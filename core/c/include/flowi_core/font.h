
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This file is auto-generated by api_gen. DO NOT EDIT!
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "idx.h"
#include "manual.h"

struct FlContext;

#ifdef __cplusplus
extern "C" {
#endif

// Allows the user to select how accurate the glyph placement should be.The list has a the fastest (CPU performance
// wise) first (Monospace) and the slowest (Accurate) lastRule of thumb is:Auto (Same as basic)Monospaced (code/fixed
// size fonts) - use Monospace modeRegular Latin text - use Basic modeHebrew and other complex languages that require
// accurate layout - Use accurate
typedef enum FlFontPlacementMode {
    // Let the library decide the mode (default)
    FlFontPlacementMode_Auto = 0,
    // Used for regular Latin based text
    FlFontPlacementMode_Basic = 0,
    // Used for fixed-width monospaces fonts (Fastest)
    FlFontPlacementMode_Mono = 1,
    // Used for accurate glyph placement (uses the Harfbuzz lib thus is the slowest mode)
    FlFontPlacementMode_Accurate = 2,
} FlFontPlacementMode;

typedef int32_t FlFont;

// Create a font from (TTF) file. To use the font use [Font::set] or [Font::set_with_size] before using text-based
// widgetsReturns >= 0 for valid handle, use fl_get_status(); for more detailed error message
FlFont fl_font_new_from_file_impl(struct FlContext* flowi_ctx, FlString filename, uint32_t font_size,
                                  FlFontPlacementMode placement_mode);

FL_INLINE FlFont fl_font_new_from_file_ctx(struct FlContext* flowi_ctx, const char* filename, uint32_t font_size,
                                           FlFontPlacementMode placement_mode) {
    FlString filename_ = {filename, 1, (uint32_t)strlen(filename)};
    return fl_font_new_from_file_impl(flowi_ctx, filename_, font_size, placement_mode);
}

#define fl_font_new_from_file(filename_, font_size, placement_mode) \
    fl_font_new_from_file_ctx(flowi_ctx, filename_, font_size, placement_mode)

// Create a font from memory. Data is expected to point to a TTF file. Fl will take a copy of this data in some
// casesLike when needing the accurate placement mode used by Harzbuff that needs to original ttf data
FlFont fl_font_new_from_memory_impl(struct FlContext* flowi_ctx, FlString name, uint8_t* data, uint32_t data_size,
                                    uint32_t font_size, FlFontPlacementMode placement_mode);

FL_INLINE FlFont fl_font_new_from_memory_ctx(struct FlContext* flowi_ctx, const char* name, uint8_t* data,
                                             uint32_t data_size, uint32_t font_size,
                                             FlFontPlacementMode placement_mode) {
    FlString name_ = {name, 1, (uint32_t)strlen(name)};
    return fl_font_new_from_memory_impl(flowi_ctx, name_, data, data_size, font_size, placement_mode);
}

#define fl_font_new_from_memory(name_, data, data_size, font_size, placement_mode) \
    fl_font_new_from_memory_ctx(flowi_ctx, name_, data, data_size, font_size, placement_mode)

// Set the font as active when drawing text
void fl_font_set_impl(struct FlContext* flowi_ctx, FlFont font);

FL_INLINE void fl_font_set_ctx(struct FlContext* flowi_ctx, FlFont font) {
    fl_font_set_impl(flowi_ctx, font);
}

#define fl_font_set(font) fl_font_set_ctx(flowi_ctx, font)

// Set font active with specific size in pixels
void fl_font_set_with_size_impl(struct FlContext* flowi_ctx, uint32_t size);

FL_INLINE void fl_font_set_with_size_ctx(struct FlContext* flowi_ctx, uint32_t size) {
    fl_font_set_with_size_impl(flowi_ctx, size);
}

#define fl_font_set_with_size(size) fl_font_set_with_size_ctx(flowi_ctx, size)

// Destory the current font, render the id invalid
void fl_font_destroy_impl(struct FlContext* flowi_ctx, FlFont font);

FL_INLINE void fl_font_destroy_ctx(struct FlContext* flowi_ctx, FlFont font) {
    fl_font_destroy_impl(flowi_ctx, font);
}

#define fl_font_destroy(font) fl_font_destroy_ctx(flowi_ctx, font)

#ifdef __cplusplus
}
#endif
