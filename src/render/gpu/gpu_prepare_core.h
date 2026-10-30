#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GPU backend: the backend-agnostic CPU core of the resource-prepare seams, shared by the GL and Metal prepares.
//
// Everything a prepare seam does before it touches an API: decode validation, the atlas-or-slot tier choice,
// glyph and SVG placement and lowering, the compaction retry around a full SVG atlas, and the UI-scale atlas
// reset. A backend supplies only the texture upload and the compute dispatch.

#include "gpu_glyph_atlas.h"
#include "gpu_image_atlas.h"
#include "gpu_image_slots.h"
#include "gpu_svg_atlas.h"
#include "gpu_svg_color_lower.h"
#include <core/types.h>
#include <ui/render_backend.h>

struct FlArena;
struct FlGlyphOutline;
struct FlVectorImage;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Image preparation.

// True when decoded is a usable decode result (non-null, positive extent, 1 or 4 channels). Logs and returns
// false otherwise.
bool gpu_prepare_image_decode_valid(FlImageLoadRequestId request_id, const FlImageDecodeResult* decoded,
                                    const FlImageLoadRequest* request);

// True when the image takes its own slot-tier texture: it was created dedicated, or a side exceeds
// GPU_IMAGE_ATLAS_MAX_DIM. Dedicated images never land in the shared atlas at any size.
bool gpu_prepare_image_wants_slot_tier(const FlImageDecodeResult* decoded, const FlImageLoadRequest* request);

// One placed atlas cell and the premultiplied RGBA8 texels to upload into it.
typedef struct GpuAtlasImageUpload {
    u32 x;
    u32 y;
    u32 w;
    u32 h;
    const u8* pixels;
    u32 stride; // bytes per source row
} GpuAtlasImageUpload;

// Place decoded into atlas under request_id and describe its upload. A 1-channel source is expanded to
// premultiplied white x alpha in scratch; a 4-channel source uploads from its own rows. Returns false when
// nothing was placed (no room, or the handle is already placed).
bool gpu_prepare_image_place_atlas(GpuImageAtlas* atlas, FlImageLoadRequestId request_id,
                                   const FlImageDecodeResult* decoded, struct FlArena* scratch,
                                   GpuAtlasImageUpload* out);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Glyph preparation.

// The padded atlas cell a glyph outline occupies. left / top are the cell's top-left corner in outline space
// (y-up), the origin gpu_glyph_lower_outline lowers against. An empty glyph - no contours, or a control box that
// rounds out to nothing - carries a zeroed cell with empty set, earning a metrics-only atlas entry and no raster
// dispatch.
typedef struct GpuGlyphCell {
    u32 pw; // padded cell width, texels (0 when empty)
    u32 ph; // padded cell height, texels (0 when empty)
    f32 left;
    f32 top;
    bool empty;
} GpuGlyphCell;

// Cell extent for outline: the control box rounded out to whole texels plus the padding ring. The origin is floored,
// not translated to 0.
GpuGlyphCell gpu_prepare_glyph_cell(const struct FlGlyphOutline* outline);

// A glyph placed in the atlas with its outline lowered to the curve buffer's quadratic wire format.
typedef struct GpuGlyphRaster {
    const GpuGlyphEntry* entry;
    GpuGlyphCell cell;
    f32* floats; // allocated from the arena passed to gpu_prepare_glyph_place
    u32 quad_count;
} GpuGlyphRaster;

// Place the glyph in atlas and lower its outline into arena. Returns true when out is filled and the backend
// must dispatch the cell; the caller rewinds arena after the dispatch. Returns false with nothing allocated for
// a null outline, an already-placed glyph, an empty glyph (metrics-only entry), or a full atlas (the wholesale
// repack is deferred via gpu_glyph_atlas_on_overflow).
bool gpu_prepare_glyph_place(GpuGlyphAtlas* atlas, struct FlArena* arena, FlFontId font, u16 size, u32 glyph_id,
                             const struct FlGlyphOutline* outline, GpuGlyphRaster* out);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Vector preparation.

// The uniform fit of a vector document onto a quantized raster size: the document's larger side lands on
// size, preserving aspect, and the cell is the fitted content extent plus the padding ring.
typedef struct GpuSvgCellFit {
    f32 scale; // document pixels -> raster pixels
    u32 pw;    // padded cell width, texels
    u32 ph;    // padded cell height, texels
} GpuSvgCellFit;

// Fit vector onto raster size size, writing the result to out. Returns false for a degenerate document
// (no extent, or one that fits to an empty cell) - nothing to place, nothing to raster.
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
// serves both a freshly placed cell's first raster and a survivor's re-raster after a compaction. Returns false
// when there is nothing to rasterize, in which case nothing was allocated.
bool gpu_prepare_svg_cell_geometry(struct FlArena* arena, const GpuSvgEntry* entry, const struct FlVectorImage* vector,
                                   bool color, GpuSvgCellGeometry* out);

// The backend's raster dispatch for one placed cell: lower it (gpu_prepare_svg_cell_geometry) and encode the
// upload + compute launch. backend is the caller's opaque shared state.
typedef void (*GpuRasterCellFn)(void* backend, const GpuSvgEntry* entry, const struct FlVectorImage* vector,
                                bool color);

// Place handle into atlas at quantized raster size size and rasterize it through raster_cell, which is
// the only backend-specific step. A handle already rasterized at this size is a no-op. On a placement failure
// the atlas is compacted wholesale, every survivor is re-rastered into its new position from its retained
// geometry, *compact_count is ticked, and the placement is retried once; an icon too big for a
// freshly-compacted atlas is dropped.
void gpu_prepare_vector_into(void* backend, GpuSvgAtlas* atlas, FlImageHandle handle, u16 size,
                             const struct FlVectorImage* vector, bool color, GpuRasterCellFn raster_cell,
                             u32* compact_count);

// Vector-image entry: pick the monochrome or multi-color atlas by vector->classification and place and raster
// through gpu_prepare_vector_into. A null or shapeless vector is a no-op.
void gpu_prepare_vector(void* backend, GpuSvgAtlas* mono_atlas, GpuSvgAtlas* color_atlas, FlImageHandle handle,
                        u16 size, const struct FlVectorImage* vector, GpuRasterCellFn raster_cell, u32* compact_count);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Backend lifecycle seams.

// Reset the glyph and both SVG atlases' placement bookkeeping when ui_scale differs from *last_ui_scale, and
// record it. An unchanged scale is a no-op.
void gpu_prepare_scale_changed(float* last_ui_scale, float ui_scale, struct FlArena* perm_arena,
                               GpuGlyphAtlas* glyph_atlas, GpuSvgAtlas* svg_atlas, GpuSvgAtlas* color_svg_atlas);

// Log the frame's dropped slot-image commands when every slot was taken.
void gpu_prepare_log_slot_overflow(const GpuImageSlots* slots);
