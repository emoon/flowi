
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
static FlFont fl_font_new_from_file(struct FlFontApi* api, const char* filename, uint32_t font_size);

// Create a font from memory. Data is expected to point to a TTF file. Fl will take a copy of this data in some cases
// Like when needing the accurate placement mode used by Harzbuff that needs to original ttf data
static FlFont fl_font_new_from_memory(struct FlFontApi* api, const char* name, uint8_t* data, uint32_t data_size,
                                      uint32_t font_size);

// Destory the current font, render the id invalid
static void fl_font_destroy(struct FlFontApi* api, FlFont font);

#include "font.inl"

#ifdef __cplusplus
}
#endif
