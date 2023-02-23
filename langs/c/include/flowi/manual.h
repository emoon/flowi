#pragma once

#include <stdint.h>
#include <string.h>
#include "inline.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Flowi representation of a String.

typedef struct FlString {
    const char* str;
    uint32_t len;
} FlString;

typedef struct FlColor {
    float r, g, b, a;
} FlColor;

const char* fl_string_to_cstr(char* temp_target, int temp_size, FlString str);

FL_INLINE FlString fl_cstr_to_flstring(const char* str) {
    FlString ret = {str, (unsigned int)strlen(str)};
    return ret;
}

struct FlContext;
struct FlInternalData;
