
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This file is auto-generated by api_gen. DO NOT EDIT!
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "manual.h"

#ifdef __cplusplus
extern "C" {
#endif

struct FlFontApi;

typedef uint64_t FlFont;

// Create a font from (TTF) file. To use the font use [ui::set_font] before using text-based widgets
// Returns >= 0 for valid handle, use fl_get_status(); for more detailed error message
static FlFont fl_font_new_from_file(const char* filename, uint32_t font_size);

// Create an new font from a FFT file with a range of characters that should be pre-generated
static FlFont fl_font_new_from_file_range(const char* filename, uint32_t font_size, uint16_t glyph_range_start,
                                          uint16_t glyph_range_end);

// Create a font from memory. Data is expected to point to a TTF file. Fl will take a copy of this data in some cases
// Like when needing the accurate placement mode used by Harzbuff that needs to original ttf data
static FlFont fl_font_new_from_memory(const char* name, uint8_t* data, uint32_t data_size, uint32_t font_size);

// Push a font for usage
static void fl_font_push(FlFont font);

// Pop a font from the stack
static void fl_font_pop();

// Destory the current font, render the id invalid
static void fl_font_destroy(FlFont font);

#include "font.inl"

#ifdef __cplusplus
}
#endif
