#pragma once

#include "../include/config.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef s32 FlFont;
struct FlContext;

// Allows the user to select how accurate the glyph placement should be.
// The list has a the fastest (CPU performance wise) first (Monospace) and the slowest (Accurate) last
//
// Rule of thumb is:
// Auto (Same as basic)
// Monospaced (code/fixed size fonts) - use Monospace mode
// Regular Latin text - use Basic mode
// Hebrew and other complex languages that require accurate layout - Use accurate
typedef enum FlFontGlyphPlacementMode {
    // Let the library decide the mode (default)
    FlFontGlyphPlacementMode_Auto = 0,
    // Used for regular Latin based text
    FlFontGlyphPlacementMode_Basic = 0,
    // Used for fixed-width monospaces fonts (Fastest)
    FlFontGlyphPlacementMode_Mono = 1,
    // Used for accurate glyph placement (uses the Harfbuzz lib thus is the slowest mode)
    FlFontGlyphPlacementMode_Accurate = 2,
} FlFontGlyphPlacementMode;

#if FL_ALLOW_STDIO
// Create a font from (TTF) file. To use the font use `fl_font_set(id)` before using text-based widgets
// GlyphRanges can be set to NULL if AtlasMode is BuildOnDemand
// Returns >= 0 for valid handle, use fl_get_status(); for more detailed error message
FlFont fl_font_create_from_file(struct FlContext* ctx, const char* filename, int size,
                                FlFontGlyphPlacementMode placement_mode);
#endif

// Create a font from memory. Data is expected to point to a TTF file. Fl will take a copy of this data in some cases
// Like when needing the accurate placement mode used by Harzbuff that needs to original ttf data
FlFont fl_font_create_from_memory(struct FlContext* ctx, const char* name, int name_len, const u8* data, u32 data_size,
                                  int font_size, FlFontGlyphPlacementMode placement_mode);

// Destroy an existing created font
void fl_font_destroy(struct FlContext* ctx, FlFont font_id);

// Set the font as active when drawing text
void fl_font_set(struct FlContext* ctx, FlFont handle);

// Set font active with specific size in pixels
void fl_font_set_with_size(struct FlContext* ctx, FlFont handle, int size);

#ifdef __cplusplus
}
#endif
