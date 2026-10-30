// -----------------------------------------------------------------------------
// Image Format Conversion for Software Renderer
//
// Converts interleaved images (RGBA, XRGB) to planar RGB format for efficient
// tile-based rendering. Adds bilinear padding (+1 width/height).
// -----------------------------------------------------------------------------

#pragma once

#include <core/core.h>
#include <ui/image.h>

struct FlArena;

// The scaled-image SIMD sampler (sr_render_image_scale.c, sample_plane_8) loads a fixed 32-byte window per plane
// row (two unaligned 16-byte loads at load_offset and load_offset + 16). Near the bottom-right of the last plane
// that window reaches up to ~30 bytes past the final plane's last byte, so every buffer the sampler can read
// (sr_convert_to_planar output and image_cache_alloc_entry blocks) is padded by this many guard bytes to keep the
// over-read in bounds. The guard bytes are only ever loaded and discarded by the shuffle - never sampled - so their
// contents never affect output.
#define SR_SCALE_PLANE_LOAD_GUARD 32

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Deinterleave a run of interleaved pixels into separate planar channel buffers
//
// Reads count pixels of src_channels bytes each; r/g/b come from bytes 0/1/2.
// When keep_alpha is set (requires src_channels == 4) the alpha byte (3) is written
// to a; otherwise a is ignored and may be nullptr.

void sr_deinterleave_pixels(const u8* src, u8* r, u8* g, u8* b, u8* a, int count, int src_channels, bool keep_alpha);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Convert a 4-channel interleaved image (RGBA or XRGB8888) to planar format
//
// Allocates a new ImageData and planar buffer from the provided arena and adds
// bilinear padding (+1 width/height). The returned ImageData has
// format=FlImageFormat_Deinterleaved and channels 3 (keep_alpha=false) or 4
// (keep_alpha=true). r/g/b read bytes 0/1/2; for XRGB the unused X byte (3) is
// simply dropped (keep_alpha=false). Returns nullptr on error.

ImageData* sr_convert_to_planar(const ImageData* src, bool keep_alpha, struct FlArena* arena);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// True if the image format requires conversion to planar.

static inline bool sr_image_needs_conversion(const ImageData* image) {
    return image != nullptr && (image->format == FlImageFormat_RGBA || image->format == FlImageFormat_XRGB);
}
