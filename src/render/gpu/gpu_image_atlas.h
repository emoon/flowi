#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GPU backend: image-atlas bookkeeping - handle -> atlas-cell map over the skyline packer.
//
// Released cells go on a reuse list and are claimed by the first later placement that fits; the skyline
// packer underneath stays append-only. A reused cell keeps its full extent; the remainder is wasted until the
// cell is released again. Images larger than GPU_IMAGE_ATLAS_MAX_DIM are rejected.

#include "gpu_atlas_pack.h"
#include <ui/ui_types.h>

struct FlArena;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define GPU_IMAGE_ATLAS_SIZE 1024   // atlas texture is GPU_IMAGE_ATLAS_SIZE^2 RGBA8
#define GPU_IMAGE_ATLAS_MAX_DIM 256 // per-image cap; larger images never enter the atlas
#define GPU_IMAGE_ATLAS_MAX_ENTRIES 256

typedef struct GpuImageAtlasEntry {
    FlImageHandle handle;
    GpuAtlasCell cell; // full claimed extent - release returns all of it to the reuse list
    u32 img_w;         // image extent within the cell (= cell.w/h for fresh placements, <= for reused cells);
    u32 img_h;         // what rendering samples
} GpuImageAtlasEntry;

typedef struct GpuImageAtlas {
    GpuImageAtlasEntry* entries; // live placements, dense (release swap-removes)
    u32 entry_count;
    GpuAtlasCell* free_cells; // released cells awaiting reuse, dense
    u32 free_count;
    GpuAtlasPack pack;
} GpuImageAtlas;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void gpu_image_atlas_init(GpuImageAtlas* atlas, struct FlArena* arena);

// Place a w x h image for handle: claim the first released cell it fits in, else skyline-insert. Returns
// NULL (nothing placed) when the image exceeds GPU_IMAGE_ATLAS_MAX_DIM, the entry table or atlas is full, or
// the handle is already placed. The returned entry is valid until the next place/release call.
const GpuImageAtlasEntry* gpu_image_atlas_place(GpuImageAtlas* atlas, FlImageHandle handle, u32 w, u32 h);

// Free the handle's cell: the placement is forgotten (the handle stops resolving) and the cell's full extent
// joins the reuse list. A handle that was never placed is a no-op.
void gpu_image_atlas_release(GpuImageAtlas* atlas, FlImageHandle handle);

// Resolve a handle to its placement. Returns NULL when the handle has none (never prepared, too large, or
// released). Valid until the next place/release call.
const GpuImageAtlasEntry* gpu_image_atlas_find(const GpuImageAtlas* atlas, FlImageHandle handle);

// Expand a single-channel coverage source into premultiplied white x alpha - rgb = alpha - in a tight RGBA8
// destination, which must hold w*h*4 bytes. src_stride is the source's bytes per row (0 = tightly packed, i.e. w).
void gpu_image_atlas_expand_coverage(u8* dst, const u8* src, u32 w, u32 h, u32 src_stride);
