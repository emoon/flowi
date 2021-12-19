#pragma once

#include <stdbool.h>
#include "types.h"
#include "../include/config.h"
#include "flowi.h"
#include "../include/flowi_render.h"    // TODO: Fixme

struct FlVertPosUvColor;
struct Glyph;

// Convert utf8 to codepoints (u16) Will return false if the input utf8 is is invalid
// Output is to be expected to be 16 byte aligned and contain 32 bytes of extra data
bool utf8_to_codepoints_u16(u16* output, const u8* input, int len);

// Convert utf8 to codepoints (u16) Will not validate that the input is legal
// Output is to be expected to be 16 byte aligned and contain 32 bytes of extra data
bool utf8_to_codepoints_u16_unsafe(u16* output, const u8* input, int len);

// Convert utf8 to codepoints (u32) Will return false if the input utf8 is is invalid
// Output is to be expected to be 16 byte aligned and contain 32 bytes of extra data
bool utf8_to_codepoints_u32(u32* output, const u8* input, int len);

// Convert utf8 to codepoints (u16) Will not validate that the input is legal
// Output is to be expected to be 16 byte aligned and contain 32 bytes of extra data
bool utf8_to_codepoints_u32_unsafe(u32* output, const u8* input, int len);

// Generate a vertex buffer given some input data
void Text_generate_vertex_buffer_ref(struct FlVertPosUvColor* FL_RESTRICT out, FlIdxSize* FL_RESTRICT index_buffer,
                                     const struct Glyph* FL_RESTRICT glyph_lookup, const u32* FL_RESTRICT codepoints, u32 color,
                                     FlVec2 pos, FlIdxSize vertex_id, int count);
