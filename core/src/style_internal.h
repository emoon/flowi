#pragma once

#include "types.h"

#define FL_RGB(r, g, b) (((u32)r) << 16 | (((u32)g) << 8) | ((u32)b))

typedef enum LengthPercentType {
    LengthPercentType_Length,
    LengthPercentType_Percent,
} LengthPercentType;

typedef struct LengthPercentValue {
    float value;
    LengthPercentType type;
} LengthPercentValue;

typedef enum Side {
    Side_Top,
    Side_Right,
    Side_Bottom,
    Side_Left,
} Side;

typedef enum Corner {
    Side_TopLeft,
    Side_TopRight,
    Side_BottomRight,
    Side_BottomLeft,
} Corner;

typedef struct Spacing {
    u16 top[4];
} Padding;

// Box styles has been modelled around a simplified version of the CSS Box Model that can be seen here
// https://www.w3schools.com/css/css_boxmodel.asp

typedef struct Border {
    LengthPercentValue border_radius[4];
    u32 colors[4];
    u16 width;
} Border;

typedef struct Style {
    Border border;
    Spacing padding;
    Spacing margin;
    u32 current_font;
    u32 background_color;
    u32 text_color;
    u32 font_size;
} Style;
