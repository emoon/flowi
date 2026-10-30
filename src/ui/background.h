#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Backend-agnostic background controller.
//
// Holds blend / alpha / clear-color state, handed to the backend as an FlBackground snapshot at the front of
// each frame (FlRenderFrame.background - see render_backend.h). This module performs no rendering.

#include "image.h"
#include <ui/render_backend.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct FlBackgroundState {
    ImageHandle current; // Current/incoming image (IMAGE_HANDLE_INVALID = none)
    ImageHandle prev;    // Previous image of a blend (IMAGE_HANDLE_INVALID = single image, no blend)
    u8 alpha;            // Current-image opacity when there is no blend: 0=transparent, 255=opaque
    u8 t;                // Blend mix: 0=full prev, 255=full current
    u8 clear_r;          // Clear color (drawn under/instead of the image)
    u8 clear_g;
    u8 clear_b;
} FlBackgroundState;

typedef struct FlBackgroundController {
    FlBackgroundState state;
} FlBackgroundController;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// Initialize with theme defaults (opaque, clear color 14/14/16, no image, no blend).
void background_controller_init(FlBackgroundController* c);

/// Set background image. Pass IMAGE_HANDLE_INVALID to clear. Resets alpha to opaque and clears any blend.
void background_controller_set(FlBackgroundController* c, ImageHandle handle);

/// Set current-image alpha (0=transparent, 255=opaque).
void background_controller_set_alpha(FlBackgroundController* c, u8 alpha);

/// Set clear color (used where no image covers).
void background_controller_set_clear_color(FlBackgroundController* c, u8 r, u8 g, u8 b);

/// Blend two images this frame: t mixes prev (0) toward current (255). Stateless - stores prev/current/t and
/// nothing else; the caller re-supplies all three each frame.
void background_controller_set_blend(FlBackgroundController* c, ImageHandle prev, ImageHandle current, u8 t);

/// Snapshot the controller into the FlBackground for one frame. A valid prev handle is the sole
/// "this is a two-image blend" signal for the backend.
FlBackground background_controller_snapshot(const FlBackgroundController* c);
