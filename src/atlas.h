#pragma once

#include "render.h"
#include "types.h"

struct FlAllocator;
struct FlGlobalState;

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// INTERNAL HEADER
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef enum AtlasImageType {
    AtlasImageType_U8,
    AtlasImageType_RGBA8,
} AtlasImageType;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct AtlasNode {
    short x, y, width;
} AtlasNode;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct Atlas {
    int width, height;
    AtlasNode* nodes;
    u8* image_data;
    int image_stride;
    int image_stride_mul;
    int count;
    int capacity;
    u32 texture_id;
    AtlasImageType image_type;
    FlRenderRect dirty_rect;
    struct FlAllocator* allocator;
} Atlas;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Atlas* Atlas_create(int w, int h, AtlasImageType image_type, struct FlGlobalState* state,
                    struct FlAllocator* allocator);
void Atlas_destroy(Atlas* self);
u8* Atlas_image_data_at(Atlas* self, int x, int y, int* stride);

// Used when starting to add/end rects to track dirty area
void Atlas_begin_add_rects(Atlas* self);
void Atlas_end_add_rects(Atlas* self, struct FlGlobalState* state);

u8* Atlas_add_rect(Atlas* self, int rw, int rh, int* rx, int* ry, int* stride);

bool Atlas_expand(Atlas* self, int w, int h);

#ifdef __cplusplus
}
#endif
