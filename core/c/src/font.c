#include <flowi_core/font.h>
#include <flowi_core/error.h>
#include <assert.h>
#include "flowi.h"
#include "font_private.h"
#include "internal.h"
#include "linear_allocator.h"
#include "render.h"
#include "utils.h"
#include "atlas.h"

// TODO: Support external functions
#include <freetype/freetype.h>
#include <math.h>

//#if fl_ALLOW_STDIO
// Create a font from (TTF) file. To use the font use [Font::set] or [Font::set_with_size] before using text-based
// widgetsReturns >= 0 for valid handle, use fl_get_status(); for more detailed error message
FlFont fl_font_new_from_file_impl(struct FlContext* flowi_ctx, FlString filename, uint32_t font_size,
                                  FlFontPlacementMode placement_mode)
{
	// TODO: Handle temp string
	if (!filename.c_string) {
		return -1;
	}

    u32 size = 0;
    u8* data = Io_load_file_to_memory(filename.str, &size);

    if (!data) {
        return -1;
    }

    return fl_font_new_from_memory_impl(flowi_ctx, filename, data, size, font_size, placement_mode);
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
	// TODO: Pass in allocator
    Font* font = FlAllocator_alloc_zero_type(ctx->global_state->global_allocator, Font);
    font->allocator = ctx->global_state->global_allocator;

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
    FL_UNUSED(placement_mode);
    // Use to store global data such as fonts, etc
    FlGlobalState* state = ctx->global_state;

    // TODO: Support threaded context
    FT_Face face;

    // Try to create the face
    FT_Error error = FT_New_Memory_Face(state->ft_library, font_data, data_size, 0, &face);
    if (error != 0) {
        ERROR_ADD(FlError_Font, "Freetype error %s when loading font: %s", FT_Error_String(error), name);
        return -1;
    }

    error = FT_Set_Pixel_Sizes(face, 0, font_size);
    if (error != 0) {
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

    if (state->font_count > FL_FONTS_MAX) {
        ERROR_ADD(FlError_Font, "Max number of fonts %d has been reached", FL_FONTS_MAX);
        return -1;
    }

    Font* font = font_create(ctx, face);

	// Hack: do proper allocator
    font->font_data_to_free = (u8*)font_data;

    if (!font) {
        ERROR_ADD(FlError_Font, "Unable to allocate memory for font: %s", "fixme");
        return -1;
    }

    font->default_size = font_size;

    int font_id = state->font_count++;
    state->fonts[font_id] = font;

    return (FlFont)font_id;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Destroy an existing created font

void fl_font_destroy_impl(struct FlContext* ctx, FlFont font_id) {
    FlGlobalState* state = ctx->global_state;

    if (font_id < 0 || font_id >= state->font_count) {
        ERROR_ADD(FlError_Font, "Tried to destroy font id %d, but id is out of range (0 - %d)", font_id,
                  state->font_count - 1);
        return;
    }

    Font* font = state->fonts[font_id];

    FT_Error error = FT_Done_Face(font->ft_face);
    if (error != 0) {
        ERROR_ADD(FlError_Font, "Freetype error %s when destroying font font: %s", FT_Error_String(error),
                  font->debug_name);
    }

    // TODO: allocator
	free(font->font_data_to_free);

    FlAllocator_free(state->global_allocator, font->glyph_info.codepoint_sizes);
    FlAllocator_free(state->global_allocator, font);

    state->fonts[font_id] = NULL;
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
        u8* data = FlAllocator_alloc(ctx->global_state->global_allocator, total_size);

        CodepointSize* codepoint_sizes = (CodepointSize*)data;
        Glyph* glyphs = (Glyph*)(codepoint_sizes + new_size);
        f32* advance_x = (f32*)(glyphs + new_size);

        // Copy the old data data if we had any
        if (old_size != 0) {
            memcpy(codepoint_sizes, info->codepoint_sizes, old_size * sizeof(CodepointSize));
            memcpy(glyphs, info->glyphs, old_size * sizeof(Glyph));
            memcpy(advance_x, info->advance_x, old_size * sizeof(f32));
            FlAllocator_free(ctx->global_state->global_allocator, info->codepoint_sizes);
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

Glyph* Font_get_glyph(const Font* self, u32 codepoint) {
    const u32 hash_idx = hash_int(codepoint) & (HASH_LUT_SIZE - 1);
    u16 hash_entry = self->lut[hash_idx];
    int size = self->default_size;	// TODO: Fixme

    while (hash_entry != 0xffff) {
        const CodepointSize* current = &self->glyph_info.codepoint_sizes[hash_entry];
        if (current->codepoint == codepoint && current->size == size) {
        	return &self->glyph_info.glyphs[hash_entry];
        }

        hash_entry = current->next_index;
    }

    return NULL;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool generate_glyph(FlContext* FL_RESTRICT ctx, Font* font, u32 codepoint, int size) {
    GlyphInfo* info = &font->glyph_info;
    // TODO: Assign atlas to a font when creating
    Atlas* atlas = ctx->global_state->mono_fonts_atlas;

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
        ERROR_ADD(FlError_Font, "Freetype error %s when loading glyph %d. font: %s", FT_Error_String(error), glyph_index,
                  font->debug_name);
        return false;
    }

	/*
    error = FT_Get_Advance(font->font, glyph, FT_LOAD_NO_SCALE, &adv_fixed);
    if (error != 0) {
        ERROR_ADD(FlError_Font, "Freetype error %s when getting advance glyph %d. font: %s", FT_Error_String(error),
                  glyph, font->debug_name);
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

    //int advance = (int)adv_fixed;
    int x0 = g->bitmap_left;
    int x1 = x0 + g->bitmap.width;
    int y0 = -g->bitmap_top;
    int y1 = y0 + g->bitmap.rows;

    int gw = x1 - x0;
    int gh = y1 - y0;

    int rx = 0, ry = 0, stride = 0;

    // Alloc slot in the atlas
    // TODO: We need to handle this in a good way
    // if we run out of space in the atlas we need to resize here

	u8* dest = Atlas_add_rect(atlas, gw, gh, &rx, &ry, &stride);
	if (!dest) {
        ERROR_ADD(FlError_Memory, "Out of space in atlas when generating glyph for font %s", font->debug_name);
        return false;
	}

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
    glyph->x_offset = (u16)x0;
    glyph->y_offset = (u16)y0;
	glyph->advance_x = (float)FT_CEIL(g->advance.x);

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Font_generate_glyphs(FlContext* FL_RESTRICT ctx, FlFont font_id, const u32* FL_RESTRICT codepoints, int count,
                          int size) {
    Font* font = ctx->global_state->fonts[font_id];

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

