#include "font.h"
#include <assert.h>
#include "../external/stb/stb_rect_pack.h"
#include "../include/error.h"
#include "flowi.h"
#include "font_private.h"
#include "internal.h"
#include "linear_allocator.h"
#include "render.h"
#include "utils.h"

// TODO: Support external functions
#include <math.h>

// TODO: Handle if we don't have freetype
#include <freetype/freetype.h>

//#if fl_ALLOW_STDIO
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Build a font from (TTF) file. To use the font use `fl_font_set(id)` before using text-based widgets
// GlyphRanges can be set to NULL if AtlasMode is BuildOnDemand
FlFont fl_font_from_file(const char* filename, int font_size, FlFontBuildMode build_mode, FlFontAtlasMode atlas_mode,
                         FlFontGlyphPlacementMode placement_mode, FlGlyphRange* ranges) {
    u32 size = 0;
    const u8* data = Io_load_file_to_memory(filename, &size);

    if (!data) {
        return -1;
    }

    return fl_font_from_memory(filename, data, size, font_size, build_mode, atlas_mode, placement_mode, ranges);
}
//#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Glyph metrics:
// --------------
//
//                       xmin                     xmax
//                        |                         |
//                        |<-------- width -------->|
//                        |                         |
//              |         +-------------------------+----------------- ymax
//              |         |    ggggggggg   ggggg    |     ^        ^
//              |         |   g:::::::::ggg::::g    |     |        |
//              |         |  g:::::::::::::::::g    |     |        |
//              |         | g::::::ggggg::::::gg    |     |        |
//              |         | g:::::g     g:::::g     |     |        |
//    offsetX  -|-------->| g:::::g     g:::::g     |  offsetY     |
//              |         | g:::::g     g:::::g     |     |        |
//              |         | g::::::g    g:::::g     |     |        |
//              |         | g:::::::ggggg:::::g     |     |        |
//              |         |  g::::::::::::::::g     |     |      height
//              |         |   gg::::::::::::::g     |     |        |
//  baseline ---*---------|---- gggggggg::::::g-----*--------      |
//            / |         |             g:::::g     |              |
//     origin   |         | gggggg      g:::::g     |              |
//              |         | g:::::gg   gg:::::g     |              |
//              |         |  g::::::ggg:::::::g     |              |
//              |         |   gg:::::::::::::g      |              |
//              |         |     ggg::::::ggg        |              |
//              |         |         gggggg          |              v
//              |         +-------------------------+----------------- ymin
//              |                                   |
//              |------------- advanceX ----------->|

// A structure that describe a glyph.
typedef struct TempGlyphInfo {
    s16 width;        // glyph's width in pixels.
    s16 height;       // glyph's height in pixels.
    s16 offset_x;     // the distance from the origin ("pen position") to the left of the glyph.
    s16 offset_y;     // the distance from the origin to the top of the glyph. this is usually a value < 0.
    int pitch;        // pitch of font data
    float advance_x;  // the distance from the origin to the origin of the next glyph. this is usually a value > 0.
} TempGlyphInfo;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
typedef struct FontAtlas {
    int font_id;
    GlyphInfo* glyph_info;
    u8* colored;
} FontAtlas;
*/

// From SDL_ttf: Handy routines for converting from fixed point
// TODO: Use proper floats here
#define FT_CEIL(X) (((X + 63) & -64) / 64)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Build a font from memory. Data is expected to point to a TTF file. Fl will take a copy of this data in some cases
// Like when needing the accurate placement mode used by Harzbuff that needs to original ttf data
FlFont fl_font_from_memory(const char* name, const u8* font_data, u32 data_size, int pixel_size,
                           FlFontBuildMode build_mode, FlFontAtlasMode atlas_mode,
                           FlFontGlyphPlacementMode placement_mode, FlGlyphRange* ranges) {
    FL_UNUSED(build_mode);
    FL_UNUSED(atlas_mode);
    FL_UNUSED(placement_mode);

    // Use to store global data such as fonts, etc
    FlGlobalState* state = g_state;

    // TODO: Support threaded context
    FT_Face face;

    // Try to create the face
    FT_Error error = FT_New_Memory_Face(state->ft_library, font_data, data_size, 0, &face);
    if (error != 0) {
        ERROR_ADD(FlError_Font, "Freetype error %s when loading font: %s", FT_Error_String(error), name);
        return -1;
    }

    error = FT_Set_Pixel_Sizes(face, 0, pixel_size);
    if (error) {
        ERROR_ADD(FlError_Font, "Freetype error %s when setting size font: %s", FT_Error_String(error), name);
        return -1;
    }

    error = FT_Select_Charmap(face, FT_ENCODING_UNICODE);
    if (error != 0) {
        ERROR_ADD(FlError_Font, "Freetype error %s when selecting charmap for font: %s", FT_Error_String(error), name);
        return -1;
    }

    if (error != 0) {
        ERROR_ADD(FlError_Font, "Freetype error %s when selecting font size for font: %s", FT_Error_String(error),
                  name);
        return -1;
    }

    LinearAllocator allocator;

    // TODO: Add support for Immeditae/DeferredMode
    // TODO: Add support for Prebuild/OnDemand mode
    // TODO: Check for overlap in the ranges for the char indices
    // TODO: Do all ranges

    // TODO: Validate ranges
    s16 start = ranges->ranges[0];
    s16 end = ranges->ranges[1] + 1;  // + 1 to make range inclusive

    // Approx alloc size needed
    int array_len = end - start;
    int memory_size = ((pixel_size * pixel_size) * array_len) * 8;
    int glyph_count = end;

    // TODO: Fix me
    u8* linear_allocator_data = malloc(memory_size);
    LinearAllocator_create(&allocator, "FreetypeBuilder", linear_allocator_data, memory_size);

    u8** bitmaps = LinearAllocator_alloc_array(&allocator, u8*, array_len);
    stbrp_rect* pack_rects = LinearAllocator_alloc_array_zero(&allocator, stbrp_rect, array_len);
    TempGlyphInfo* glyph_infos = LinearAllocator_alloc_array(&allocator, TempGlyphInfo, array_len);

    // 1 pixel padding between each glyph
    const int padding = 1;
    int area = 0;
    int gylph_id = start;

    for (int i = 0; i < array_len; ++i, ++gylph_id) {
        error = FT_Load_Char(face, gylph_id, FT_LOAD_RENDER);
        if (error != 0) {
            ERROR_ADD(FlError_Font, "Freetype error %s when loading char %d (0x%x) for font: %s",
                      FT_Error_String(error), gylph_id, gylph_id, name);
            free(linear_allocator_data);
            return -1;
        }

        TempGlyphInfo* info = &glyph_infos[i];

        const FT_GlyphSlot slot = face->glyph;
        const FT_Bitmap* ft_bitmap = &face->glyph->bitmap;

        // Make sure we can fit these in s16. No error handling as this shouldn't happen
        assert((int)ft_bitmap->width >= 0 && ft_bitmap->width < 0x7fff);
        assert((int)ft_bitmap->rows >= 0 && ft_bitmap->rows < 0x7fff);
        // assert(slot->bitmap_left >= 0 && slot->bitmap_left < 0x7fff);
        // assert(slot->bitmap_top >= 0 && slot->bitmap_top < 0x7fff);

        info->width = (s16)ft_bitmap->width;
        info->height = (s16)ft_bitmap->rows;
        info->offset_x = (s16)slot->bitmap_left;
        info->offset_y = 140 - (s16)slot->bitmap_top;
        info->height = (s16)ft_bitmap->rows;
        info->pitch = ft_bitmap->pitch;
        info->advance_x = (float)FT_CEIL(slot->advance.x);

        const int width = ft_bitmap->width;
        const int height = ft_bitmap->rows;
        const u8* src = ft_bitmap->buffer;
        const int src_pitch = ft_bitmap->pitch;

        switch (ft_bitmap->pixel_mode) {
            // Grayscale image, 1 byte per pixel.
            case FT_PIXEL_MODE_GRAY: {
                u8* bitmap = LinearAllocator_alloc_array(&allocator, u8, height * width);
                bitmaps[i] = bitmap;

                if (FL_LIKELY(src_pitch == width)) {
                    memcpy(bitmap, ft_bitmap->buffer, width * height);
                } else {
                    for (int h = 0; h < height; ++h, src += src_pitch, bitmap += width) {
                        memcpy(bitmap, src, width);
                    }
                }

                break;
            }

            case FT_PIXEL_MODE_BGRA: {
                // RGBA 4-bytes per-pixel
                // TODO: How to deal with pre-mul alpha? Either convert, or have renderer handle it
                u8* bitmap = LinearAllocator_alloc_array(&allocator, u8, height * width * 4);
                bitmaps[i] = bitmap;

                if (FL_LIKELY(src_pitch * 4 == width)) {
                    memcpy(bitmap, ft_bitmap->buffer, width * height * 4);
                } else {
                    for (int h = 0; h < height; ++h, src += src_pitch, bitmap += width) {
                        memcpy(bitmap, src, width * 4);
                    }
                }

                break;
            }

            default: {
                ERROR_ADD(FlError_Font, "Freetype error. Pixel mode %d not supported for font: %s",
                          ft_bitmap->pixel_mode, name);
                free(linear_allocator_data);
                return -1;
            }
        }

        const int w = ft_bitmap->width + padding;
        const int h = ft_bitmap->rows + padding;

        pack_rects[i].id = i;
        pack_rects[i].w = (stbrp_coord)w;
        pack_rects[i].h = (stbrp_coord)h;

        area += w * h;
    }

    // TODO: Don't rely on mathlib
    const int area_t = (int)(sqrt(area) * 0.7f);
    const int texture_width = round_to_next_pow_two(area_t);
    const int texture_max_height = 1024 * 32;

    // printf("texture width %d\n", texture_width);

    // Allocate temp data for rect packer
    stbrp_node* pack_nodes = LinearAllocator_alloc_array(&allocator, stbrp_node, array_len);

    // Pack the the rectangles
    stbrp_context pack_context;
    stbrp_init_target(&pack_context, texture_width, texture_max_height, pack_nodes, array_len);
    stbrp_pack_rects(&pack_context, pack_rects, array_len);

    int texture_height = -1;

    // figure out min max for texture
    // TODO: SIMD

    for (int i = 0; i < array_len; ++i) {
        const stbrp_rect* rect = &pack_rects[i];
        if (rect->was_packed) {
            texture_height = FL_MAX(rect->y + rect->h, texture_height);
        }

        // printf("packed id %04d - [%04d %04d - %04d %04d]\n", rect->id, rect->y, rect->x, rect->x + rect->w, rect->y +
        // rect->h);
    }

    texture_height = round_to_next_pow_two(texture_height);

    // TODO: Custom allocator
    Font* font = (Font*)calloc(1, sizeof(Font));
    font->advance_x = (f32*)calloc(1, sizeof(f32) * glyph_count);
    font->glyphs = (Glyph*)calloc(1, sizeof(Glyph) * glyph_count);
    font->glyph_count = glyph_count;

    // TODO: Handle different glyph formats (RGBA etc)

    u8* texture_data = (u8*)calloc(1, texture_width * texture_height);

    // update the texture atlas and the glyph lookup
    for (int i = 0; i < array_len; ++i) {
        const stbrp_rect* rect = &pack_rects[i];
        const TempGlyphInfo* info = &glyph_infos[i];

        if (!rect->was_packed) {
            continue;
        }

        const u32 id = rect->id;
        const u8* bitmap = bitmaps[id];
        const u32 height = rect->h - padding;
        const u32 width = rect->w - padding;

        // TODO: Handle RGBA data
        u8* temp_data = texture_data + (rect->y * texture_width) + rect->x;

        for (u32 y = 0; y < height; ++y, bitmap += width, temp_data += texture_width) {
            memcpy(temp_data, bitmap, width);
        }

        const int tx = rect->x;  // + padding;
        const int ty = rect->y;  // + padding;

        int x0 = info->offset_x;
        int y0 = info->offset_y;
        int x1 = x0 + info->width;
        int y1 = y0 + info->height;

        // TODO: SIMD
        font->glyphs[id].x0 = x0;
        font->glyphs[id].y0 = y0;
        font->glyphs[id].x1 = x1;
        font->glyphs[id].y1 = y1;

        // calc uv coords in normalized 0.0 - 1.0 space
        font->glyphs[id].u0 = tx;
        font->glyphs[id].v0 = ty;
        font->glyphs[id].u1 = tx + info->width;
        font->glyphs[id].v1 = ty + info->height;

        font->glyphs[id].advance_x = info->advance_x;
        font->advance_x[id] = info->advance_x;
    }

    // TODO: Make sure we select the correct texture format here
    FlRcCreateTexture* texture = Render_create_texture_static(state, texture_data);

#if FL_VALIDATE_RANGES
    if (!texture) {
        ERROR_ADD(FlError_Font, "Failed to create font: %s because crate_texture_static failed.", filename);

        return -1;
    }
#endif

    texture->format = FlTextureFormat_R8_LINEAR;
    texture->width = texture_width;
    texture->height = texture_height;

    // TODO: Custom allocator
    free(linear_allocator_data);

    int font_id = state->font_count++;

    if (state->font_count > FL_FONTS_MAX) {
        ERROR_ADD(FlError_Font, "Max number of fonts %d has been reached", FL_FONTS_MAX);
        return -1;
    }

    state->fonts[font_id] = font;

    return (FlFont)font_id;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Font_init(FlGlobalState* state) {
    //#if defined(fl_FONTLIB_FREETYPE)
    int error = FT_Init_FreeType(&state->ft_library);
    FL_UNUSED(error);
    //#elif defined(fl_FONTLIB_STBTYPE)
    //#endif
}
