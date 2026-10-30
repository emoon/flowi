#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GPU backend: vector-icon draw routing - a resolved SVG atlas cell -> FlCmdImage. An icon crosses the IR as an
// ordinary image command.

#include "gpu_atlas_pack.h"
#include <ui/render_ir.h>
#include <ui/vector_image.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Build the FlCmdImage draw mapping for a prepared vector icon resolved to its live atlas cell (the full padded
// extent). Routing follows the classification:
//   monochrome  -> alpha-coverage mode (1), carrying tint
//   multi-color -> color mode (0), white tint, so the baked RGBA stands as-is
// src is the cell's content sub-rect (the padding border inset); dst is the caller's destination rect.
FlCmdImage gpu_svg_image_cmd(FlImageHandle handle, GpuAtlasCell cell, FlVectorClass cls, FlColor tint, FlRect dst);
