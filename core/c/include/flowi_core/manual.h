#pragma once

#include <stdint.h>
#include "inline.h"

struct FlContext;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Flowi representation of a String.

typedef struct FlString {
    const char* str;
    int len;
} FlString;
