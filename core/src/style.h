#pragma once

#include "types.h"

typedef enum LengthPercentType {
    LengthPercentType_Length,
    LengthPercentType_Percent,
} LengthPercentType;


typedef struct LengthPercentValue {
    float value;
    LengthPercentType type;
} LengthPercentValue;

//border-top-left-radius, border-top-right-radius, border-bottom-right-radius, and border-bottom-left-radius

typedef struct Style {
    u32 current_font;
    u32 background_color;
} Style;
