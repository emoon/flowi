#pragma once

#include <flowi/ui.h>
#include "command_buffer.h"
#include "vertex_allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

struct FlAllocator;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct Layer {
    VertexAllocator vertex_allocator;
    CommandBuffer primitive_commands;
} Layer;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TODO: Pass in size of vertex and index buffers allocators

//void Layers_create(Layers* self, struct FlAllocator* allocator);
//void Layers_destroy(Layers* self);

#ifdef __cplusplus
}
#endif
    
