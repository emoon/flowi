#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GPU backend: skyline-bottom-left rect packer. Append-only - rects are placed, never removed;
// clients layer handle bookkeeping and released-cell reuse on top.

#include <core/types.h>

struct FlArena;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The skyline is a left-to-right list of segments; segment i spans x in [nodes[i].x, nodes[i+1].x) (the last
// runs to the atlas width) at height nodes[i].y (the first free row above the packed rects in that span).

typedef struct GpuAtlasPackNode {
    u32 x;
    u32 y;
} GpuAtlasPackNode;

typedef struct GpuAtlasCell {
    u32 x;
    u32 y;
    u32 w;
    u32 h;
} GpuAtlasCell;

typedef struct GpuAtlasPack {
    GpuAtlasPackNode* nodes; // capacity width + 1 (a node per texel column is the worst case)
    u32 node_count;
    u32 width;
    u32 height;
} GpuAtlasPack;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Initialize (or reset) a packer over a width x height atlas. Node storage comes from arena on first init;
// re-initializing with the same width reuses it.
void gpu_atlas_pack_init(GpuAtlasPack* pack, struct FlArena* arena, u32 width, u32 height);

// Place a w x h rect bottom-left-most (lowest resulting top edge, then leftmost). Returns false when no
// position fits; out_x/out_y are written only on success. Zero-sized rects are rejected.
bool gpu_atlas_pack_insert(GpuAtlasPack* pack, u32 w, u32 h, u32* out_x, u32* out_y);
