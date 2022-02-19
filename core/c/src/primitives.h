#pragma once

#include <flowi_core/math_data.h>
#include "command_buffer.h"
#include "flowi.h"
#include "style.h"

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
    Primitive_DrawBox,
    Primitive_DrawCircle,
} Primitive;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Used for rendering text

typedef struct PrimitiveText {
    // Font to be used when generating the text
    struct Font* font;
    // utf8 text
    const char* text;
    // Where to draw the primitive. TODO: Separate data stream for positions
    FlVec2 position;
    // Size of the font when rendering
    u32 font_size;
    // length of the text
    int len;
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

typedef struct PrimitiveBox {
    // Index into global position list
    int position_index;
    // Index into size list
    int size_index;
} PrimitiveBox;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define Primitive_alloc_text(state) \
    (PrimitiveText*)CommandBuffer_alloc_cmd(&state->primitive_commands, Primitive_DrawText, sizeof(PrimitiveText))

#define Primitive_alloc_image(state) \
    (PrimitiveImage*)CommandBuffer_alloc_cmd(&state->primitive_commands, Primitive_DrawImage, sizeof(PrimitiveImage))
