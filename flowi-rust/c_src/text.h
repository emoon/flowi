#pragma once

#if 0

#include <flowi/config.h>
#include <flowi/error.h>
#include <stdbool.h>
#include "flowi.h"
#include "render.h"
#include "types.h"

struct FlVertPosUvColor;
struct Glyph;
struct Font;
struct LinearAllocator;

typedef struct Utf8Result {
    u32* codepoints;
    int len;
    FlError error;
} Utf8Result;

// Convert utf8 to codepoints (u32) Will return false if the input utf8 is is invalid
// Output is to be expected to be 16 byte aligned and contain 32 bytes of extra data
Utf8Result Utf8_to_codepoints_u32(struct LinearAllocator* temp_allocator, const u8* input, int len);

// Convert utf8 to codepoints (u16) Will not validate that the input is legal
// Output is to be expected to be 16 byte aligned and contain 32 bytes of extra data
bool utf8_to_codepoints_u32_unsafe(u32* output, const u8* input, int len);

// Generate a vertex buffer given some input data
void Text_generate_vertex_buffer_ref(struct FlVertPosUvColor* FL_RESTRICT out, FlIdxSize* FL_RESTRICT index_buffer,
                                     const struct Font* FL_RESTRICT font, u32 font_size,
                                     const u32* FL_RESTRICT codepoints, u32 color, FlVec2 pos, FlIdxSize vertex_id,
                                     int count);

// Calculate AABB for the text
FlVec2 Text_calculate_size(const struct Glyph* FL_RESTRICT glyph_lookup, const u32* FL_RESTRICT codepoints, int count);

#endif
