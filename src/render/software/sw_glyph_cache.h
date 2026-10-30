#pragma once

#include <core/types.h>
#include <ui/ui_ids.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Software glyph realization.
//
// The software backend's private glyph cache: it takes the shared font layer's raster source (FreeType bytes,
// a bitmap descriptor, or the built-in default font) and rasterizes glyphs to sized coverage bitmaps, with a
// job-threaded per-size atlas. This is where FreeType's hinted grayscale/LCD rasterizer is called - never in
// the shared ui/ font layer.

struct FlArena;
struct FlFontCache;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Rasterization target: grayscale or LCD sub-pixel coverage.

typedef enum FontRenderMode {
    FontRenderMode_Grayscale,
    FontRenderMode_LCD,
} FontRenderMode;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A realized glyph: sized coverage bitmap(s) plus positioning.

typedef struct Glyph {
    // Only r is set if the glyph is grayscale
    u8* bitmap_data_r;
    u8* bitmap_data_g;
    u8* bitmap_data_b;

    // Positioning data (26.6 fractional pixels)
    int advance_x;
    int bearing_x; // Horizontal offset from pen to glyph origin
    int bearing_y; // Vertical offset from pen to glyph origin

    // Metrics data (26.6 fractional pixels)
    int height_metrics; // Typographic height for text sizing

    i16 pixel_width; // Displayed width in pixels (an LCD bitmap row holds 3 samples per pixel)
    i16 width;       // Bitmap row stride in bytes
    i16 height;
} Glyph;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct SwGlyphCache SwGlyphCache;

// Create a SW glyph cache bound to the shared font layer. shared provides face management, metrics, and the
// raster source for each font id.
SwGlyphCache* sw_glyph_cache_new(struct FlArena* arena, struct FlFontCache* shared, FontRenderMode render_mode);
void sw_glyph_cache_destroy(SwGlyphCache* self);

// Resolve a (font, size, codepoint) to a realized coverage glyph, rasterizing/caching on demand. font_size is
// the exact whole-px raster size. Returns nullptr if the font is unknown, not ready, or the glyph is
// unavailable.
//
// Main thread only: on-demand rasterization and lazy face creation use the cache's own FreeType library,
// which has no internal locking.
const Glyph* sw_glyph_cache_get(SwGlyphCache* self, FlFontId font_id, u16 font_size, u32 codepoint);
