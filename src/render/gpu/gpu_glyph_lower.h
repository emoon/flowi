#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GPU backend: outline -> quadratic lowering for the glyph-raster dispatch.
//
// Turns the shared font layer's outline (font_cache.h FlGlyphOutline - pixels, y-up, pen-relative) into the curve
// SSBO's wire format: 3 points (6 f32) per quadratic Bezier - p0, control, p1 - in padded-cell-local texel
// coordinates, y-down. Lines take their midpoint as control (exact); cubics split by de Casteljau halving twice
// into 4 quadratics each, with the 3/4-rule control per piece. The y flip reverses winding, which the shader's
// |acc| absorbs.

#include <core/types.h>

struct FlGlyphOutline;
struct FlGlyphSegment;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Worst-case quadratics per outline segment (a cubic's 4) - sizes the caller's output buffer:
// segment_count * GPU_GLYPH_LOWER_MAX_QUADS_PER_SEGMENT * 6 f32.
#define GPU_GLYPH_LOWER_MAX_QUADS_PER_SEGMENT 4

// Lower a raw segment soup into out_floats. (origin_x, origin_y) is the outline-space position of the
// padded cell's top-left texel corner: cell x = p.x - origin_x, cell y = origin_y - p.y. Returns the number
// of quadratics written. Shared by the glyph path (font_cache outlines) and the SVG vector path (nanosvg
// contours adapted to FlGlyphSegment).
u32 gpu_glyph_lower_segments(const struct FlGlyphSegment* segments, u32 segment_count, f32 origin_x, f32 origin_y,
                             f32* out_floats);

// Lower outline into out_floats.
u32 gpu_glyph_lower_outline(const struct FlGlyphOutline* outline, f32 origin_x, f32 origin_y, f32* out_floats);
