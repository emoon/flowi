#pragma once

// GPU backend: the backend-agnostic CPU core of the resource-prepare seams, shared by the GL and Metal prepares.
//
// Everything a prepare seam does before it touches an API: decode validation, the atlas-or-slot tier choice,
// glyph placement and lowering, and the UI-scale atlas reset. A backend supplies
// only the texture upload and the compute dispatch.

#include "gpu_glyph_atlas.h"
#include "gpu_image_atlas.h"
#include "gpu_image_slots.h"
#include "gpu_cpu_state.h"
#include <core/types.h>
#include <ui/render_backend.h>

struct FlArena;
struct FlGlyphOutline;

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
// premultiplied white x alpha in scratch; a 4-channel source uploads from its own rows. Returns a zeroed upload
// (pixels null) when nothing was placed (no room, or the handle is already placed).
GpuAtlasImageUpload gpu_prepare_image_place_atlas(GpuImageAtlas* atlas, FlImageLoadRequestId request_id,
                                                  const FlImageDecodeResult* decoded, struct FlArena* scratch);

// Glyph preparation.

// The padded atlas cell a glyph outline occupies. left / top are the cell's top-left corner in outline space
// (y-up), the origin gpu_glyph_lower_outline lowers against. An empty glyph (no contours, or a control box that
// rounds out to nothing) carries a zeroed cell with empty set, earning a metrics-only atlas entry and no raster
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

// Place the glyph in atlas and lower its outline into arena. Returns a raster with entry set when the backend
// must dispatch the cell; the caller rewinds arena after the dispatch. Returns a zeroed raster (entry null) with
// nothing allocated for a null outline, an already-placed glyph, an empty glyph (metrics-only entry), or a full
// atlas (the wholesale repack is deferred via gpu_glyph_atlas_on_overflow).
GpuGlyphRaster gpu_prepare_glyph_place(GpuGlyphAtlas* atlas, struct FlArena* arena, GpuGlyphKey key,
                                       const struct FlGlyphOutline* outline);

// Vector preparation.

// Backend lifecycle seams.

// Reset the glyph and both SVG atlases' placement bookkeeping when ui_scale differs from *last_ui_scale, and
// record it. An unchanged scale is a no-op.
void gpu_prepare_scale_changed(GpuCpuState* cpu, float* last_ui_scale, float ui_scale, struct FlArena* perm_arena);

// Log the frame's dropped slot-image commands when every slot was taken.
void gpu_prepare_log_slot_overflow(const GpuImageSlots* slots);
