#include <assert.h>
#include <flowi_core/error.h>
#include <flowi_core/font.h>
#include "atlas.h"
#include "flowi.h"
#include "font_private.h"
#include "internal.h"
#include "linear_allocator.h"
#include "render.h"
#include "utils.h"

// TODO: Support external functions
#include <freetype/freetype.h>
#include <freetype/ftadvanc.h>
#include <freetype/ftglyph.h>
#include <math.h>

//#if fl_ALLOW_STDIO
// Create a font from (TTF) file. To use the font use [Font::set] or [Font::set_with_size] before using text-based
// widgetsReturns >= 0 for valid handle, use fl_get_status(); for more detailed error message
FlFont fl_font_new_from_file_impl(struct FlContext* ctx, FlString filename, uint32_t font_size,
                                  FlFontPlacementMode placement_mode) {
    u32 size = 0;
    u8* data = Io_load_file_to_memory_flstring(ctx, filename, &size);

    if (!data) {
        return 0;
    }

    return fl_font_new_from_memory_impl(ctx, filename, data, size, font_size, placement_mode);
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

// From SDL_ttf: Handy routines for converting from fixed point
// TODO: Use proper floats here
#define FT_CEIL(X) (((X + 63) & -64) / 64)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static Font* font_create(FlContext* ctx, FT_Face face) {
    Font* font = Handles_create_handle(&ctx->global->font_handles);
    font->allocator = ctx->global->global_allocator;

    u64 handle = font->handle;

    memset(font, 0, sizeof(Font));

    font->handle = handle;

    if (!font) {
        return NULL;
    }

    font->ft_face = face;

    for (int i = 0; i < HASH_LUT_SIZE; ++i) {
        font->lut[i] = -1;
    }

    return font;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Build a font from memory. Data is expected to point to a TTF file. Fl will take a copy of this data in some cases
// Like when needing the accurate placement mode used by Harzbuff that needs to original ttf data
FlFont fl_font_new_from_memory_impl(struct FlContext* ctx, FlString name, uint8_t* font_data, uint32_t data_size,
                                    uint32_t font_size, FlFontPlacementMode placement_mode) {
    Font* font = NULL;
    FL_UNUSED(placement_mode);
    // Use to store global data such as fonts, etc
    FlGlobalState* state = ctx->global;

    // TODO: Support threaded context
    FT_Face face;

    // Try to create the face
    FT_Error error = FT_New_Memory_Face(state->ft_library, font_data, data_size, 0, &face);
    if (error != 0) {
        ERROR_ADD(FlError_Font, "Freetype error %s when loading font: %s", FT_Error_String(error), name);
        return 0;
    }

    error = FT_Set_Pixel_Sizes(face, 0, font_size);
    if (error != 0) {
        ERROR_ADD(FlError_Font, "Freetype error %s when setting size font: %s", FT_Error_String(error), name);
        return 0;
    }

    error = FT_Select_Charmap(face, FT_ENCODING_UNICODE);
    if (error != 0) {
        ERROR_ADD(FlError_Font, "Freetype error %s when selecting charmap for font: %s", FT_Error_String(error), name);
        return 0;
    }

    if (error != 0) {
        ERROR_ADD(FlError_Font, "Freetype error %s when selecting font size for font: %s", FT_Error_String(error),
                  name);
        return 0;
    }

    font = font_create(ctx, face);

    // Hack: do proper allocator
    font->font_data_to_free = (u8*)font_data;

    if (!font) {
        ERROR_ADD(FlError_Font, "Unable to allocate memory for font: %s", "fixme");
        return 0;
    }

    const int ascent = font->ft_face->ascender;
    const int descent = font->ft_face->descender;
    const int fh = ascent - descent;

    font->ascender = (float)ascent / (float)fh;
    font->descender = (float)descent / (float)fh;
    font->default_size = font_size;

    return (FlFont)font->handle;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_font_set_impl(struct FlContext* ctx, FlFont font) {
    ctx->current_font = Handles_get_data(&ctx->global->font_handles, font);

    if (!ctx->current_font) {
        ERROR_ADD(FlError_Font, "Unable to get font handle %x has it been deleted?. Using default font", font)
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Font_destroy(FlGlobalState* state, Font* font) {
    if (font->handle == 0)
        return;

    FT_Error error = FT_Done_Face(font->ft_face);
    if (error != 0) {
        ERROR_ADD(FlError_Font, "Freetype error %s when destroying font font: %s", FT_Error_String(error),
                  font->debug_name);
    }

    // TODO: allocator
    free(font->font_data_to_free);

    FlAllocator_free(state->global_allocator, font->glyph_info.codepoint_sizes);

    font->handle = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Destroy an existing created font

void fl_font_destroy_impl(struct FlContext* ctx, FlFont font_id) {
    FlGlobalState* state = ctx->global;

    Font* font = Handles_get_data(&ctx->global->font_handles, font_id);

    if (!font) {
        ERROR_ADD(FlError_Font, "Unable to delete font handle %x has it been already been deleted?", font)
        return;
    }

    Font_destroy(state, font);

    Handles_remove_handle(&ctx->global->font_handles, font_id);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Hash a codepoint to allow us faster lookups

FL_INLINE u32 hash_int(u32 a) {
    a += ~(a << 15);
    a ^= (a >> 10);
    a += (a << 3);
    a ^= (a >> 6);
    a += ~(a << 11);
    a ^= (a >> 16);
    return a;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TODO: Use glyph allocator

static int allocate_glyph(FlContext* FL_RESTRICT ctx, Font* font) {
    GlyphInfo* info = &font->glyph_info;

    if (info->count + 1 > info->capacity) {
        const int old_size = info->capacity;
        const int new_size = old_size == 0 ? 16 : old_size * 2;

        int total_size = (sizeof(Glyph) + sizeof(CodepointSize) + sizeof(f32)) * new_size;

        // TODO: Handle OOM
        // we batch all of the allocations into and get the new ranges.
        // As we do the batching like this we can't use regular realloc so we manually memcopy the old data
        u8* data = FlAllocator_alloc(ctx->global->global_allocator, total_size);

        CodepointSize* codepoint_sizes = (CodepointSize*)data;
        Glyph* glyphs = (Glyph*)(codepoint_sizes + new_size);
        f32* advance_x = (f32*)(glyphs + new_size);

        // Copy the old data data if we had any
        if (old_size != 0) {
            memcpy(codepoint_sizes, info->codepoint_sizes, old_size * sizeof(CodepointSize));
            memcpy(glyphs, info->glyphs, old_size * sizeof(Glyph));
            memcpy(advance_x, info->advance_x, old_size * sizeof(f32));
            FlAllocator_free(ctx->global->global_allocator, info->codepoint_sizes);
        }

        info->codepoint_sizes = codepoint_sizes;
        info->glyphs = glyphs;
        info->advance_x = advance_x;

        info->capacity = new_size;
    }

    int index = info->count++;

    return index;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Find glyph given codepoint
// TODO: Check performance of this code

Glyph* Font_get_glyph(const Font* self, u32 codepoint, u32 font_size) {
    const u32 hash_idx = hash_int(codepoint) & (HASH_LUT_SIZE - 1);
    u16 hash_entry = self->lut[hash_idx];

    while (hash_entry != 0xffff) {
        const CodepointSize* current = &self->glyph_info.codepoint_sizes[hash_entry];
        if (current->codepoint == codepoint && current->size == font_size) {
            return &self->glyph_info.glyphs[hash_entry];
        }

        hash_entry = current->next_index;
    }

    return NULL;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool generate_glyph(FlContext* FL_RESTRICT ctx, Font* font, u32 codepoint, int size) {
    GlyphInfo* info = &font->glyph_info;
    // int adv_fixed = 0;
    const int pad = 2;

    // TODO: Assign atlas to a font when creating
    Atlas* atlas = ctx->global->mono_fonts_atlas;

    const CodepointSize* codepoint_sizes = info->codepoint_sizes;

    // TODO: :Perf: Unpack all codepoints to an array first as it allows the compiler to generate vector instructions
    // instead
    const u32 hash_idx = hash_int(codepoint) & (HASH_LUT_SIZE - 1);
    u16 hash_entry = font->lut[hash_idx];

    // find glyph if it exists already
    while (hash_entry != 0xffff) {
        const CodepointSize* current = &codepoint_sizes[hash_entry];

        // already generated
        if (current->codepoint == codepoint && current->size == size) {
            return false;
        }

        hash_entry = current->next_index;
    }

    // Find a glyph for the codepoint
    // TODO: Add fallback options

    int glyph_index = FT_Get_Char_Index(font->ft_face, codepoint);
    if (glyph_index == 0) {
        // ..
    }

    FT_Error error = FT_Set_Pixel_Sizes(font->ft_face, 0, size);
    if (error != 0) {
        ERROR_ADD(FlError_Font, "Freetype error %s when setting size. font: %s", FT_Error_String(error),
                  font->debug_name);
        return false;
    }

    error = FT_Load_Glyph(font->ft_face, glyph_index, FT_LOAD_RENDER);
    if (error != 0) {
        ERROR_ADD(FlError_Font, "Freetype error %s when loading glyph %d. font: %s", FT_Error_String(error),
                  glyph_index, font->debug_name);
        return false;
    }

    /*
    error = FT_Get_Advance(font->ft_face, glyph_index, FT_LOAD_NO_SCALE, &adv_fixed);
    if (error != 0) {
        ERROR_ADD(FlError_Font, "Freetype error %s when getting advance glyph %d. font: %s", FT_Error_String(error),
                  glyph_index, font->debug_name);
        return false;
    }
    */

    // Alloc glyph and insert into hashtable
    int alloc_index = allocate_glyph(ctx, font);

    info->codepoint_sizes[alloc_index].codepoint = codepoint;
    info->codepoint_sizes[alloc_index].size = size;
    info->codepoint_sizes[alloc_index].next_index = font->lut[hash_idx];
    font->lut[hash_idx] = alloc_index;

    Glyph* glyph = &info->glyphs[alloc_index];

    FT_GlyphSlot g = font->ft_face->glyph;

    // int advance = (int)adv_fixed;
    int x0 = g->bitmap_left;
    int x1 = x0 + g->bitmap.width;
    int y0 = -g->bitmap_top;
    int y1 = y0 + g->bitmap.rows;

    int gw = (x1 - x0) + (pad * 2);
    int gh = (y1 - y0) + (pad * 2);

    int rx = 0, ry = 0, stride = 0;

    // Alloc slot in the atlas
    // TODO: We need to handle this in a good way
    // if we run out of space in the atlas we need to resize here

    u8* dest = Atlas_add_rect(atlas, gw, gh, &rx, &ry, &stride);
    if (!dest) {
        ERROR_ADD(FlError_Memory, "Out of space in atlas when generating glyph for font %s", font->debug_name);
        return false;
    }

    // adjust for padding. Assume that the area has been cleared already
    dest += pad + (pad * stride);

    const u8* src = g->bitmap.buffer;
    const int src_pitch = g->bitmap.pitch;

    // TODO: Handle the case when we have colors here also
    for (int y = 0, rows = g->bitmap.rows; y < rows; ++y) {
        memcpy(dest, src, g->bitmap.width);
        dest += stride;
        src += src_pitch;
    }

    glyph->x0 = rx;
    glyph->y0 = ry;
    glyph->x1 = rx + gw;
    glyph->y1 = ry + gh;
    glyph->x_offset = (u16)(x0 - pad);
    glyph->y_offset = (u16)(y0 - pad);
    // glyph->advance_x = (adv_scale * adv_fixed);
    glyph->advance_x = g->advance.x / 64.0f;

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_font_set_with_size_impl(struct FlContext* ctx, uint32_t size) {
    const uint32_t font_max_size = 400;
    if (size < font_max_size) {
        ctx->current_font_size = size;
    } else {
        ERROR_ADD(FlError_Font, "Unable to set font size: %d (larger than max %d)", size, font_max_size);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Font_generate_glyphs(FlContext* FL_RESTRICT ctx, Font* font, const u32* FL_RESTRICT codepoints, int count,
                          int size) {
    for (int i = 0; i < count; ++i) {
        const u32 codepoint = *codepoints++;
        generate_glyph(ctx, font, codepoint, size);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Font_init(FlGlobalState* state) {
    //#if defined(fl_FONTLIB_FREETYPE)
    int error = FT_Init_FreeType(&state->ft_library);
    FL_UNUSED(error);
    //#elif defined(fl_FONTLIB_STBTYPE)
    //#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlIVec2 Font_calc_text_size(struct FlContext* ctx, const u32* codepoints, int len) {
    Font* font = ctx->current_font;
    int font_size = ctx->current_font_size;

    FlIVec2 size = {0};

    // TODO: Handle different directions

    for (int i = 0; i < len; ++i) {
        const u32 codepoint = *codepoints++;

        Glyph* glyph = Font_get_glyph(font, codepoint, font_size);

        if (glyph) {
            size.x += (int)glyph->advance_x;  // TODO: valudate if this is correct
            size.y = FL_MAX(size.y, (glyph->y1 - glyph->y0));
        } else {
            // If we don't find the glyph (happens when not generated get)
            // We get the outline only, and do the full rendering later (optionally) multi-threaded.

            int glyph_index = FT_Get_Char_Index(font->ft_face, codepoint);
            if (glyph_index == 0) {
                // Glyph not found in font,
                printf("codepoint %d wasn't found in font, skipping...\n", codepoint);
                continue;
            }

            FT_Error error = FT_Set_Pixel_Sizes(font->ft_face, 0, font_size);
            if (error != 0) {
                ERROR_ADD(FlError_Font, "Freetype error %s when setting size. font: %s", FT_Error_String(error),
                          font->debug_name);
                continue;
            }

            error = FT_Load_Glyph(font->ft_face, glyph_index, FT_LOAD_DEFAULT);
            if (error != 0) {
                ERROR_ADD(FlError_Font, "Freetype error %s when loading glyph %d. font: %s", FT_Error_String(error),
                          glyph_index, font->debug_name);
                continue;
            }

            FT_Glyph glyph;

            error = FT_Get_Glyph(font->ft_face->glyph, &glyph);
            if (error != 0) {
                ERROR_ADD(FlError_Font, "Freetype error %s when getting glyph glyph %d. font: %s",
                          FT_Error_String(error), glyph_index, font->debug_name);
                continue;
            }

            FT_BBox bbox;
            FT_Glyph_Get_CBox(glyph, FT_GLYPH_BBOX_PIXELS, &bbox);

            int width = bbox.xMax - bbox.xMin;
            int height = bbox.yMax - bbox.yMin;

            size.x += width;
            size.y = FL_MAX(size.y, height);

            FT_Done_Glyph(glyph);
        }
    }

    return size;
}
