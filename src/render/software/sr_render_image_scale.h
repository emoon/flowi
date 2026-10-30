#pragma once

#include <core/simd.h>
#include <core/types.h>
#include "software_renderer_internal.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scaled image rendering: nearest-neighbor sampling via SIMD shuffles. The shuffle tables must already have been
// set up during command creation.

void sr_render_image_scale(i16* tile_output, const RenderCommand* command, f32x4 scissor_rect, const TileInfo* tile);
