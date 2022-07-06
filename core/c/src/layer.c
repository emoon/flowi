#include "layer.h"
#include "allocator.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
void Layers_create(Layers* self, struct FlAllocator* allocator) {
    int vertex_sizes[VertexAllocType_SIZEOF] = {1024 * 1024, 1024 * 1024};
    int index_sizes[VertexAllocType_SIZEOF] = {512 * 1024, 512 * 1024};

    for (u32 i = 0; i < FlLayerType_Count; ++i) {
        VertexAllocator_create(&self->vertex_allocators[i], allocator, vertex_sizes, index_sizes, true);
    }
}
*/

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
void Layers_destroy(Layers* self) {
    for (u32 i = 0; i < FlLayerType_Count; ++i) {
        VertexAllocator_destroy(&self->vertex_allocators[i]);
    }
}
*/
