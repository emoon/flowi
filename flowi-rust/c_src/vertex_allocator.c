#include "vertex_allocator.h"

#if 0

#include <flowi_core/error.h>
#include <string.h>


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool VertexAllocator_create(VertexAllocator* self, FlAllocator* allocator,
                            int vertex_initial_sizes[VertexAllocType_SIZEOF],
                            int index_initial_sizes[VertexAllocType_SIZEOF], bool allow_realloc) {
    memset(self, 0, sizeof(VertexAllocator));

    for (u32 f = 0; f < FL_FRAME_HISTORY; ++f) {
        for (u32 va = 0; va < VertexAllocType_SIZEOF; ++va) {
            LinearAllocator* vertex_allocator = &self->allocators[f][va][VertexStreamAllocatorType_Vertex];
            LinearAllocator* indices_allocator = &self->allocators[f][va][VertexStreamAllocatorType_Index];

            LinearAllocator_create_with_allocator(vertex_allocator, "vertex allocator", allocator,
                                                  vertex_initial_sizes[va], allow_realloc);
            LinearAllocator_create_with_allocator(indices_allocator, "indices allocator", allocator,
                                                  index_initial_sizes[va], allow_realloc);
        }
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void VertexAllocator_destroy(VertexAllocator* self) {
    for (u32 f = 0; f < FL_FRAME_HISTORY; ++f) {
        for (u32 va = 0; va < VertexAllocType_SIZEOF; ++va) {
            for (u32 st = 0; st < VertexStreamAllocatorType_SIZEOF; ++st) {
                LinearAllocator_destroy(&self->allocators[f][va][st]);
            }
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool VertexAllocator_alloc(VertexAllocator* self, VertexAllocType alloc_type, u8** verts, u8** indices, int verts_size,
                           int idx_size) {
    LinearAllocator* vertex_alloc = &self->allocators[self->frame_index][alloc_type][VertexStreamAllocatorType_Vertex];
    LinearAllocator* index_alloc = &self->allocators[self->frame_index][alloc_type][VertexStreamAllocatorType_Index];

    u8* verts_out = LinearAllocator_internal_alloc(vertex_alloc, verts_size, 4);
    u8* index_out = LinearAllocator_internal_alloc(index_alloc, idx_size, sizeof(FlIdxSize));

    // self->index_offset += idx_size;

    *verts = verts_out;
    *indices = index_out;

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void VertexAllocator_end_frame(VertexAllocator* self) {
    self->frame_index = (self->frame_index + 1) % FL_FRAME_HISTORY;
    self->index_offset = 0;

    // Rewind all allocators to the start
    for (u32 f = 0; f < FL_FRAME_HISTORY; ++f) {
        for (u32 va = 0; va < VertexAllocType_SIZEOF; ++va) {
            for (u32 st = 0; st < VertexStreamAllocatorType_SIZEOF; ++st) {
                LinearAllocator_rewind(&self->allocators[f][va][st]);
            }
        }
    }
}

#endif
