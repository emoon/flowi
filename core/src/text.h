#pragma once

#include "types.h"
#include <stdbool.h>

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

