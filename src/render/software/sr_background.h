#pragma once

#include <core/simd.h>
#include <core/types.h>
#include <ui/image/image.h>

typedef struct BackgroundState {
    ImageHandle image_handle;      // Handle for current/incoming image
    ImageHandle prev_image_handle; // Handle for previous/outgoing image (crossfade)
    u8 alpha;                      // Current image alpha: 0=transparent, 255=opaque (default: 255)
    u8 crossfade_t;                // Crossfade progress: 0=full prev, 255=full current
    bool crossfade_active;
    u8 clear_r; // Clear color, used when no background image
    u8 clear_g;
    u8 clear_b;
} BackgroundState;

struct ImageData;

// A frame's background after its images were looked up: only images that are ready and cover the surface remain.
typedef struct SrBackground {
    const struct ImageData* image;
    const struct ImageData* prev_image;
    u8 alpha;
    u8 crossfade_t;
    u8 clear_r;
    u8 clear_g;
    u8 clear_b;
    bool can_crossfade;
    bool can_direct_copy;
} SrBackground;

SrBackground sr_background_resolve(const BackgroundState* state, int surface_width, int surface_height);

// Fill the planar tile buffer with the background for the tile at tile_aabb.
void sr_background_fill_tile(const SrBackground* background, i16* tile_buffer, f32x4 tile_aabb);

// Write the background for a tile straight into the interleaved RGB output, skipping the planar tile buffer.
// Returns false, writing nothing, when the background has no such path (clear color, RGBA, alpha < 255).
bool sr_background_write_direct(const SrBackground* background, u8* output, int output_pitch, f32x4 tile_aabb);
