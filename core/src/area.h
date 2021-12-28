#pragma once

#include "types.h"
#include "flowi.h"
#include "../include/flowi_render.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Areas are used for generating an area around a widget/control.
// This can be the outline for a button, box, etc.
// The look rounding/colors/etc is determined by the current style that has been set
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FlContext;
struct FlStyle;
struct FlVertPosColor;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Area contains info about where the actual content should be place and such (with margins calculated)

typedef struct Area {
    // Generated vertices in local space
    struct FlVertPosColor* vertex_buffer;
    // Generated vertices in local space
    FlIdxSize* index_buffer;
    // Numer of vertices for the area
    int vertex_count;
    // Numer of triangle indices for the area
    int index_count;
    // Marign of the area
    FlVec2 margin_start;
    FlVec2 margin_end;
    // Area of the content
    FlVec2 content_start;
    FlVec2 content_end;
} Area;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Area* Area_generate(struct FlContext* ctx, const struct FlStyle* style, FlVec2 size);
