#pragma once

#include <flowi/math_data.h>
#include <flowi/style.h>
#include "command_buffer.h"
#include "flowi_internal.h"

struct Font;
struct FlContext;
struct FlGlobalState;
struct ImagePrivate;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// INTERNAL HEADER ONLY!
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Primitives that is used in vertex generation pass
typedef enum Primitive {
    Primitive_DrawText = 1,
    Primitive_DrawImage,
    Primitive_DrawRect,
    Primitive_DrawCircle,
} Primitive;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Used for rendering text

typedef struct PrimitiveText {
    // Font to be used when generating the text
    struct Font* font;
    // Decoded utf8-text
    u32* codepoints;
    // Number of codepoints
    int codepoint_count;
    // Where to draw the primitive. TODO: Separate data stream for positions
    FlVec2 position;
    // Size of the font when rendering
    u32 font_size;
    // Index into global position list
    int position_index;
} PrimitiveText;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct PrimitiveImage {
    // Font to be used when generating the text
    struct ImagePrivate* image;
    // Where to draw the primitive. TODO: Separate data stream for positions
    FlVec2 position;
    // Area of the image
    FlVec2 size;
} PrimitiveImage;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Used for rendering text

typedef struct PrimitiveRect {
    // Border style of th box
    //FlBorder border;
    // Postion of the box
    FlVec2 pos;
    // Size of the box
    FlVec2 size;
    // TODO: Color type
    u32 color;
} PrimitiveRect;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define Primitive_alloc_rect(layer) \
    (PrimitiveRect*)CommandBuffer_alloc_cmd(&layer->primitive_commands, Primitive_DrawRect, sizeof(PrimitiveRect))

#define Primitive_alloc_text(state) \
    (PrimitiveText*)CommandBuffer_alloc_cmd(&layer->primitive_commands, Primitive_DrawText, sizeof(PrimitiveText))

#define Primitive_alloc_image(state) \
    (PrimitiveImage*)CommandBuffer_alloc_cmd(&layer->primitive_commands, Primitive_DrawImage, sizeof(PrimitiveImage))

