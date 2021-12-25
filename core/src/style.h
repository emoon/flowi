#pragma once

#include "types.h"
#include <stdbool.h>

struct FlContext;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define FL_RGB(r, g, b) (((u32)r) << 16 | (((u32)g) << 8) | ((u32)b))
#define FL_RGB_RED FL_RGB(255, 0, 0)
#define FL_RGB_WHITE FL_RGB(255, 255, 255)
#define FL_RGB_BLACK FL_RGB(0, 0, 0)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef enum FlLengthPercentType {
    FlLengthPercentType_Length,
    FlLengthPercentType_Percent,
} LengthPercentType;

typedef struct FlLengthPercentValue {
    float value;
    LengthPercentType type;
} FlLengthPercentValue;

typedef enum FlSide {
    Side_Top,
    Side_Right,
    Side_Bottom,
    Side_Left,
} FlSide;

typedef enum Corner {
    Side_TopLeft,
    Side_TopRight,
    Side_BottomRight,
    Side_BottomLeft,
} FlCorner;

typedef struct FlSpacing {
    u16 sides[4];
} FlSpacing;

// Box styles has been modelled around a simplified version of the CSS Box Model that can be seen here
// https://www.w3schools.com/css/css_boxmodel.asp

typedef struct FlBorder {
    FlLengthPercentValue border_radius[4];
    u32 colors[4];
    u16 width;
    bool active;
} FlBorder;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct FlStyle {
    // private internal data
    void* priv;
    FlBorder border;
    FlSpacing padding;
    FlSpacing margin;
    u32 current_font;
    u32 background_color;
    u32 text_color;
    u32 font_size;
} FlStyle;

// Create a style to apply changes to with an optional name
FlStyle* fl_style_create_name_len(struct FlContext* ctx, const char* name, int name_len);

// Get the default style, not intended for
FlStyle* fl_style_get_default(struct FlContext* ctx);

// end changes made to the current style
void fl_style_end_changes(FlStyle* style);

// Called before making changes to an existing style after making it with end_changes
void fl_style_begin_changes(FlStyle* style);

// Select the style to be used, to end using the style use 'fl_pop_style()'
void fl_push_style(FlStyle* style);

// Pops the current style
void fl_pop_style();

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper macros

#define fl_style_create(ctx, name) fl_style_create_name_len(ctx, name, name != NULL ? strlen(name) : 0)

