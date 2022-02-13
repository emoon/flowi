#pragma once

#include <stdint.h>
#include "inline.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Flowi representation of a String.

typedef struct FlString {
    const char* str;
    unsigned int c_string : 1;
    unsigned int len : 31;
} FlString;

typedef struct FlColor {
    uint32_t color;
} FlColor;