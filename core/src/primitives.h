#pragma once

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
    int font_handle;
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

u8* Primitive_alloc_cmd(struct FlGlobalState* state, Primitive cmd, int size);

#define Primitive_alloc_text(state) \
    (PrimitiveText*)Primitive_alloc_cmd(state, Primitive_DrawText, sizeof(PrimitiveText) + 1)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Used to build up the render state

typedef struct BuildPrimitives {
    u8* data;
    u8* start_data;
    u8* end_data;
} BuildPrimitives;

