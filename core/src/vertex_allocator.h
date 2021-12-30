#pragma once

#include "../include/flowi_render.h"
#include "allocator.h"
#include "linear_allocator.h"
#include "types.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// INTERNAL HEADER
// Used for allocating vertex and index data for internal "rendering"
// (i.e generating vertex/index buffers for the GPU backend)
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef enum VertexStreamAllocatorType {
    VertexStreamAllocatorType_Vertex,
    VertexStreamAllocatorType_Index,
    VertexStreamAllocatorType_SIZEOF,  // Should always be last
} VertexStreamAllocatorType;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef enum VertexAllocType {
    VertexAllocType_PosColor,
    VertexAllocType_PosUvColor,
    VertexAllocType_SIZEOF,  // Should always be last
} VertexAllocType;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct VertexAllocator {
    LinearAllocator allocators[FL_FRAME_HISTORY][VertexAllocType_SIZEOF][VertexStreamAllocatorType_SIZEOF];
    int frame_index;
} VertexAllocator;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct VertsCounts {
    int vertex_count;
    int index_count;
} VertsCounts;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool VertexAllocator_create(VertexAllocator* self, FlAllocator* allocator,
                            int vertex_initial_sizes[VertexAllocType_SIZEOF],
                            int index_intial_sizes[VertexAllocType_SIZEOF], bool allow_realloc);

void VertexAllocator_end_frame(VertexAllocator* self);

void VertexAllocator_destroy(VertexAllocator* self);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool VertexAllocator_alloc(VertexAllocator* self, VertexAllocType alloc_type, u8** verts, u8** indices, int verts_size,
                           int idx_size);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get the number of vertices and indices in an allocator

FL_INLINE VertsCounts VertexAllocator_get_counts(VertexAllocator* self, VertexAllocType alloc_type,
                                                 int vert_type_size) {
    LinearAllocator* vertex_alloc = &self->allocators[self->frame_index][alloc_type][VertexStreamAllocatorType_Vertex];
    LinearAllocator* index_alloc = &self->allocators[self->frame_index][alloc_type][VertexStreamAllocatorType_Index];

    VertsCounts t = {LinearAllocator_current_position(vertex_alloc) / vert_type_size,
                     LinearAllocator_current_position(index_alloc) / sizeof(FlIdxSize)};

    return t;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define VertexAllocator_alloc_pos_color(self, verts, indices, vert_count, index_count) \
    VertexAllocator_alloc(self, VertexAllocType_PosColor, (u8**)verts, (u8**)indices,  \
                          vert_count * sizeof(FlVertPosColor), index_count * sizeof(FlIdxSize))

#define VertexAllocator_alloc_pos_uv_color(self, verts, indices, vert_count, index_count) \
    VertexAllocator_alloc(self, VertexAllocType_PosUvColor, (u8**)verts, (u8**)indices,   \
                          vert_count * sizeof(FlVertPosUvColor), index_count * sizeof(FlIdxSize))

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define VertexAllocator_get_pos_color_counts(self) \
    VertexAllocator_get_counts(self, VertexAllocType_PosColor, sizeof(FlVertPosColor))

#define VertexAllocator_get_pos_uv_color_counts(self) \
    VertexAllocator_get_counts(self, VertexAllocType_PosUvColor, sizeof(FlVertPosUvColor))
