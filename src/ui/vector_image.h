#pragma once

#include <stddef.h>

#include "font_cache.h"
#include "render_ir.h"

// Neutral vector payload handed to a backend's prepare_vector.
typedef struct FlVectorShape {
    FlGlyphSegment* segments;
    uint32_t segment_count;
    FlColor fill;
    uint8_t fill_rule;
    float opacity;
} FlVectorShape;

typedef enum FlVectorClass {
    FlVectorClass_Monochrome,
    FlVectorClass_MultiColor,
} FlVectorClass;

typedef struct FlVectorImage {
    FlVectorShape* shapes;
    uint32_t shape_count;
    FlVectorClass classification;
    FlColor mono_color;
    float width;
    float height;
} FlVectorImage;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shared vector layer - the SVG analog of the shared font layer that produces FlGlyphOutline.
//
// Parses an SVG once (via nanosvg) into a neutral, resolution-independent form - per-shape closed cubic
// contours, each with a resolved solid fill color, fill rule, and opacity - and NEVER rasterizes. The layer
// owns this geometry for the handle's lifetime, so it can be re-handed to the backend on every zoom step and
// to every per-window backend. The GPU backend rasterizes the contours into a zoom-regenerated SVG atlas; the
// SW backend ignores this layer and keeps decoding SVGs to RGBA via image_decode (decode_svg).
//
// Contours are stored as the same FlGlyphSegment soup the glyph layer uses, so the GPU cubic->quadratic
// lowering (gpu_glyph_lower) is shared verbatim. Coordinates are float document pixels, y-UP (nanosvg's
// y-down document space is flipped at parse, matching the glyph convention the lowering expects).

struct FlArena;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Parse SVG bytes into a neutral vector image, allocated from arena (which must outlive every use - the
// shared layer hands these contours back to the backend on every zoom step). Never rasterizes.
//
// Returns true on success. Returns false (and logs) when the SVG is unsupported for the GPU vector path -
// any gradient fill or any stroke on a visible shape rejects the whole image (per-image granularity) - when
// it carries no visible solid fill, or when nanosvg fails to parse it.
bool vector_image_parse(const u8* data, size_t size, struct FlArena* arena, FlVectorImage* out);
