#pragma once

#include <stdint.h>
#include <string.h>
#include "inline.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Flowi representation of a String.

typedef struct FlString {
    const char* str;
    unsigned int c_string;
    unsigned int len;
} FlString;

typedef struct FlColor {
    uint32_t color;
} FlColor;

const char* fl_string_to_cstr(char* temp_target, int temp_size, FlString str);

FL_INLINE FlString fl_cstr_to_flstring(const char* str) {
    FlString ret = {str, 1, (unsigned int)strlen(str)};
    return ret;
}
