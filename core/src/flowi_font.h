#pragma once

#include "types.h"
#include "../include/config.h"

typedef s32 FlFont;

// Set when the font atlast should be built.
typedef enum FlFontBuildMode {
    // Create the font atlas directly on the current thread.
    FlFontBuildMode_Immediate,
    // Deffer the generation making it possible to schedule on a thread later.
    FlFontBuildMode_Deferred,
} FlFontBuildMode;

// Determins how glyphs should be build (i.e when used or prebuilt)
typedef enum FlFontAtlasMode {
    // Pre-generate a range of glyphs. Has higher initial cost, but lower cost during run-time.
    // This option is good to use when a certain amount of characters will be used repeatedly.
    // For Latin based languages this is a good option
    FlFontAtlasMode_PrebildGlyphs,
    // Generated Glyphs as they are needed. This can be a good option for more complex languages which doesn't need every single glyph upfront and a very small subset will be used
    // Chinese / Hebrew and similar complex glyph sets this may be a good option
    FlFontAtlasMode_BuildOnDemand,
} FlFontAtlasMode;

// Allows the user to select how accurate the glyph placement should be.
// The list has a the fastest (CPU performance wise) first (Monospace) and the slowest (Accurate) last
//
// Rule of thumb is:
// Monospaced (code/fixed size fonts) - use Monospace mode
// Regular Latin text - use Basic mode
// Hebrew and other complex languages that require accurate layout - Use accurate
typedef enum FlFontGlyphPlacementMode {
    // Used for fixed-width monospaces fonts (Fastest)
    FlFontGlyphPlacementMode_Monospace,
    // Used for regular Latin based text (Fastest)
    FlFontGlyphPlacementMode_Basic,
    // Used for accurate glyph placement (uses the Harfbuzz lib thus is the slowest mode)
    FlFontGlyphPlacementMode_Accurate,
} FlFontGlyphPlacementMode;

// List of ranges to generate fonts.
// 0x020 - 0x127 for Latin for example
typedef struct FlGlyphRange {
    u16* ranges;
    u32 count;
} FlGlyphRange;

#if FL_ALLOW_STDIO
// Build a font from (TTF) file. To use the font use `fl_font_set(id)` before using text-based widgets
// GlyphRanges can be set to NULL if AtlasMode is BuildOnDemand
// Returns >= 0 for valid handle, use fl_get_status(); for more detailed error message
FlFont fl_font_from_file(
    const char* filename,
    int size,
    FlFontBuildMode build_mode,
    FlFontAtlasMode atlas_mode,
    FlFontGlyphPlacementMode placement_mode,
    FlGlyphRange* glyph_ranges);
#endif

// Build a font from memory. Data is expected to point to a TTF file. Fl will take a copy of this data in some cases
// Like when needing the accurate placement mode used by Harzbuff that needs to original ttf data
FlFont fl_font_from_memory(
    const char* name,
    const u8* data,
    u32 data_size,
    int size,
    FlFontBuildMode build_mode,
    FlFontAtlasMode atlas_mode,
    FlFontGlyphPlacementMode placement_mode,
    FlGlyphRange* glyph_ranges);

