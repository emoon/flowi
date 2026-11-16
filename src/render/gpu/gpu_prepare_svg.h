#pragma once

#include "gpu_svg_atlas.h"
#include "gpu_svg_color_lower.h"
#include <core/types.h>
#include <ui/render_backend.h>

struct FlArena;
struct FlVectorImage;

// The uniform fit of a vector document onto a quantized raster size: the document's larger side lands on
// size, preserving aspect, and the cell is the fitted content extent plus the padding ring.
typedef struct GpuSvgCellFit {
    f32 scale; // document pixels -> raster pixels
    u32 pw;    // padded cell width, texels
    u32 ph;    // padded cell height, texels
} GpuSvgCellFit;

// Fit vector onto raster size size, writing the result to out. Returns false for a degenerate document
// (no extent, or one that fits to an empty cell).
bool gpu_prepare_svg_fit(const struct FlVectorImage* vector, u16 size, GpuSvgCellFit* out);

// One live SVG cell's retained geometry, lowered into the curve buffer's quadratic wire format (6 f32 per
// quadratic: p0, control, p1, in padded-cell-local y-down texels). shapes / shape_count are filled only on the
// multi-color path; the monochrome path lowers every shape's contours into one undifferentiated soup.
typedef struct GpuSvgCellGeometry {
    f32* floats;
    u32 quad_count;
    GpuColorShape* shapes;
    u32 shape_count;
} GpuSvgCellGeometry;

// Lower the cell entry's geometry (vector, its retained source) for a monochrome (color false) or multi-color
// (color true) raster, allocating the wire buffers from arena. The scale comes off the entry's own size, so this
// serves both a freshly placed cell's first raster and a survivor's re-raster after a compaction. Returns a
// zeroed geometry (floats null) when there is nothing to rasterize, in which case nothing was allocated.
GpuSvgCellGeometry gpu_prepare_svg_cell_geometry(struct FlArena* arena, const GpuSvgEntry* entry,
                                                 const struct FlVectorImage* vector, bool color);

// The backend's raster dispatch for one placed cell: lower it (gpu_prepare_svg_cell_geometry) and encode the
// upload + compute launch. backend is the caller's opaque shared state.
typedef void (*GpuRasterCellFn)(void* backend, const GpuSvgEntry* entry, const struct FlVectorImage* vector,
                                bool color);

typedef struct GpuSvgRaster {
    void* backend;
    GpuRasterCellFn fn;
} GpuSvgRaster;

typedef struct GpuSvgKey {
    FlImageHandle handle;
    u16 size;
} GpuSvgKey;

// Place key into atlas and rasterize it through raster, which is the only backend-specific step. A handle
// already rasterized at this size is a no-op. On a placement failure the atlas is compacted wholesale, every
// survivor is re-rastered into its new position from its retained geometry, and the placement is retried once;
// an icon too big for a freshly-compacted atlas is dropped. Returns true when the atlas was compacted.
bool gpu_prepare_vector_into(const GpuSvgRaster* raster, GpuSvgAtlas* atlas, GpuSvgKey key,
                             const struct FlVectorImage* vector);

// Vector-image entry: pick the monochrome or multi-color atlas of the GpuCpuState backend by vector->classification
// and place and raster through gpu_prepare_vector_into, ticking svg_compact_count on a compaction. A null or
// shapeless vector is a no-op.
void gpu_prepare_vector(void* backend, GpuRasterCellFn raster_cell, GpuSvgKey key, const struct FlVectorImage* vector);
