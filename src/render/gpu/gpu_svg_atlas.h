#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GPU backend: SVG-atlas bookkeeping - (image handle, quantized size) -> atlas cell over the skyline packer,
// with one live cell per handle.
//
// Cells are keyed by (handle, quantized size), but a handle resolves to its single CURRENT live cell: a size-step
// change appends a fresh cell and ORPHANS the prior one. Orphans stay in place - the packer underneath is
// append-only - until gpu_svg_atlas_compact reclaims them on a failed placement.

#include "gpu_atlas_pack.h"
#include <ui/ui_types.h>
#include <ui/vector_image.h>

struct FlArena;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define GPU_SVG_ATLAS_SIZE_MONO 4096  // R8 monochrome atlas extent (square): 16 MB
#define GPU_SVG_ATLAS_SIZE_COLOR 2048 // RGBA8 multi-color atlas extent (square): 16 MB

// Transparent texel border inside each cell, so linear sampling and the outermost AA ramp never read a
// neighboring icon.
#define GPU_SVG_ATLAS_PAD 1

// Max live plus orphaned cells per atlas.
#define GPU_SVG_ATLAS_MAX_ENTRIES 4096

// Texel format the atlas's cells are rasterized and sampled in. The bookkeeping below deals in cells, never texels,
// and never branches on the format.
typedef enum GpuSvgTexelFormat {
    GpuSvgTexel_R8,    // single-channel coverage (monochrome icons)
    GpuSvgTexel_Rgba8, // baked four-channel color (multi-color icons)
} GpuSvgTexelFormat;

// Atlas texture extent (square side, in texels) for the format. The packer over each atlas is sized from this.
u32 gpu_svg_atlas_size(GpuSvgTexelFormat format);

typedef struct GpuSvgEntry {
    FlImageHandle handle;
    u16 size;                    // quantized raster size the icon was rasterized at (svg_quantize_size)
    bool live;                   // true for the handle's current cell; false once a newer size orphaned it
    GpuAtlasCell cell;           // full padded extent in the atlas
    FlVectorClass cls;           // the prepared icon's classification (monochrome vs multi-color)
    FlColor mono_color;          // authored fill color for a monochrome icon - the default draw tint (unused otherwise)
    const FlVectorImage* vector; // retained geometry source, owned by the shared vector layer and stable for the
                                 // handle's lifetime. Never dereferenced here; the backend re-rasters this cell
                                 // from it after a compaction.
} GpuSvgEntry;

typedef struct GpuSvgAtlas {
    GpuSvgEntry* entries; // dense, append-only (orphaned cells remain until compaction reclaims them)
    u32 entry_count;
    GpuAtlasPack pack;
    GpuSvgTexelFormat format; // the texel format this atlas's cells live in (set at init)
    struct FlArena* arena;    // create-time arena; compaction resets the packer over the existing node storage,
                              // allocating nothing new
} GpuSvgAtlas;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// SVG-specific geometric size ladder: quantizes a physical display size onto discrete raster steps, so a
// continuously-zooming host re-rasters only at step boundaries. Clamped to a PER-CLASS ceiling - a multi-color icon
// caps at 1024 (the RGBA8 atlas is 2048^2), a monochrome one at 2048 (the R8 atlas is 4096^2). Monotonic
// non-decreasing and idempotent at a step.
u16 svg_quantize_size(u16 size, FlVectorClass cls);

// The builder-side bridge from a draw's physical destination size to a raster size on the ladder above: quantize
// the dst size here to key the prepare seam, then draw the resolved cell at the exact dst. Quantization is on the
// UNIFORM size - the larger destination side - matching the prepare seam's aspect-fit of the icon's larger document
// side onto the raster size. A non-uniform dst is not re-rastered per-axis; the single cell is sampled and
// stretched. cls clamps the ladder to its per-class ceiling.
u16 svg_raster_size_for_dst(f32 dst_w, f32 dst_h, FlVectorClass cls);

// Initialize (or reset, for wholesale regeneration) the bookkeeping for an atlas in texel format format. Storage
// comes from arena on first init and is reused after.
void gpu_svg_atlas_init(GpuSvgAtlas* atlas, struct FlArena* arena, GpuSvgTexelFormat format);

// Place the icon handle at quantized raster size size occupying a w x h padded cell, recording its classification
// cls, authored fill mono_color (the monochrome draw default; ignored for multi-color), and retained geometry
// source vector. If the handle's current live cell already matches size, that cell is returned unchanged and the
// recorded classification, color, and geometry stand. Otherwise a new cell is appended and any prior live cell for
// the handle is orphaned. Returns NULL when the entry table is full or the packer refuses. The returned entry is
// valid until the next place call.
const GpuSvgEntry* gpu_svg_atlas_place(GpuSvgAtlas* atlas, FlImageHandle handle, u16 size, u32 w, u32 h,
                                       FlVectorClass cls, FlColor mono_color, const FlVectorImage* vector);

// Reclaim orphaned cells: repack only the currently-live cells into a fresh skyline, dropping every orphan. This
// moves cells, never texels. Each surviving cell keeps its (handle, size, classification, color, geometry) and only
// its position changes, so the texture data still sits at the pre-compaction positions - the backend must
// re-raster every surviving cell from its retained geometry afterward. After this returns every entry is live.
void gpu_svg_atlas_compact(GpuSvgAtlas* atlas);

// Resolve a handle to its single current live cell. Returns NULL when nothing live was prepared for it.
// Valid until the next place call.
const GpuSvgEntry* gpu_svg_atlas_find(const GpuSvgAtlas* atlas, FlImageHandle handle);
