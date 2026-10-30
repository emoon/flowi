#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GPU backend: glyph-atlas bookkeeping - (font, size, glyph) -> R8 atlas cell over the skyline packer.
//
// Append-only like the packer underneath: a glyph is placed once per (font, quantized size, glyph id) and never
// released. There is no per-glyph eviction; re-initializing this module regenerates the atlas wholesale.

#include "gpu_atlas_pack.h"
#include <ui/ui_types.h>

struct FlArena;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Atlas texture extent (square): GPU_GLYPH_ATLAS_SIZE^2 R8 texels (16 MB).
#define GPU_GLYPH_ATLAS_SIZE 4096

// Zero-coverage texel border inside each cell, so linear sampling and the outermost AA ramp never read a
// neighboring glyph.
#define GPU_GLYPH_ATLAS_PAD 1

#define GPU_GLYPH_ATLAS_MAX_ENTRIES 2048

typedef struct GpuGlyphEntry {
    FlFontId font;
    u16 size; // quarter-px fixed-point size key the outline was fetched at (light hinting is size-dependent)
    u32 glyph_id;
    GpuAtlasCell cell; // full padded extent in the atlas; w == h == 0 for empty glyphs (e.g. space)
    // Float pixels relative to the pen position on the baseline (y-up, FreeType bearing convention).
    f32 left;
    f32 top;
    f32 advance;
} GpuGlyphEntry;

typedef struct GpuGlyphAtlas {
    GpuGlyphEntry* entries; // dense, append-only
    u32 entry_count;
    GpuAtlasPack pack;
    // Set when a placement overflows the atlas; the backend honors it at the end of render() via
    // gpu_glyph_atlas_wipe_if_needed, never mid-frame.
    bool needs_wipe;
} GpuGlyphAtlas;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Initialize (or reset, for wholesale regeneration) the bookkeeping. Storage comes from arena on first
// init and is reused after.
void gpu_glyph_atlas_init(GpuGlyphAtlas* atlas, struct FlArena* arena);

// Place a glyph whose padded raster extent is w x h texels; 0 x 0 registers a metrics-only entry (an empty
// glyph occupies no atlas space). Returns NULL when the key is already placed, the entry table is full, or
// the packer refuses (the atlas is full). The returned entry is valid until the next place call.
const GpuGlyphEntry* gpu_glyph_atlas_place(GpuGlyphAtlas* atlas, FlFontId font, u16 size, u32 glyph_id, u32 w, u32 h,
                                           f32 left, f32 top, f32 advance);

// Resolve a key to its placement. Returns NULL when nothing was prepared for it. Valid until the next place
// call.
const GpuGlyphEntry* gpu_glyph_atlas_find(const GpuGlyphAtlas* atlas, FlFontId font, u16 size, u32 glyph_id);

// Record that a placement overflowed the atlas, flagging a wholesale wipe for the next frame boundary.
void gpu_glyph_atlas_on_overflow(GpuGlyphAtlas* atlas);

// Honor a deferred overflow wipe: if needs_wipe is set, reset the bookkeeping wholesale and clear the flag, so the
// next frame's prepare pass repopulates a fresh atlas; otherwise a no-op. Called once per frame at the end of
// render().
void gpu_glyph_atlas_wipe_if_needed(GpuGlyphAtlas* atlas, struct FlArena* arena);
