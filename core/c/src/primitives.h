#pragma once

#include "command_buffer.h"
#include "flowi.h"
#include "style.h"

struct Font;
struct FlContext;
struct FlGlobalState;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// INTERNAL HEADER ONLY!
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Primitives that is used in vertex generation pass
typedef enum Primitive {
    Primitive_DrawText = 1,
    Primitive_DrawBox,
    Primitive_DrawCircle,
} Primitive;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Used for rendering text

typedef struct PrimitiveText {
    // Font to be used when generating the text
    struct Font* font;
    // Size of the font when rendering
    u32 font_size;
    // utf8 text
    const char* text;
    // length of the text
    int len;
    // Index into global position list
    int position_index;
} PrimitiveText;

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
