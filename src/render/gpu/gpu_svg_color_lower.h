#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GPU backend: multi-color SVG -> per-shape quad ranges + baked premultiplied colors for the single-dispatch
// color-raster shader.
//
// Lowers each shape's contours independently through the same cubic->quadratic path the glyph and monochrome paths
// use (gpu_glyph_lower_segments), recording per shape a contiguous [quad_start, quad_count) range into the shared
// curve buffer plus a baked premultiplied color and fill rule.

#include <core/types.h>

struct FlArena;
struct FlVectorImage;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// One shape's wire record in the Shapes SSBO, matching a GLSL std430 uvec4 (16-byte stride). The color shader loops
// these in document order, accumulating signed-area coverage over [quad_start, quad_count), folds the fill rule,
// then over-blends color * coverage.
typedef struct GpuColorShape {
    u32 quad_start; // index of the shape's first quadratic in the shared curve buffer
    u32 quad_count; // number of quadratics the shape lowered to (0 for a contourless shape)
    u32 color;      // premultiplied RGBA8, R in the low byte (GLSL unpackUnorm4x8 order); opacity baked in
    u32 fill_rule;  // 0 = nonzero, 1 = even-odd (FlVectorShape.fill_rule)
} GpuColorShape;

_Static_assert(sizeof(GpuColorShape) == 16, "GpuColorShape is the std430 uvec4 wire record (16-byte stride)");

// Lower every shape of vector into the shared curve buffer out_floats (6 f32 per quadratic: p0, control,
// p1, in padded-cell-local y-down texels) and fill out_shapes with one GpuColorShape per shape, writing the
// shape count to out_shape_count. Returns the total quadratic count written.
//
// Each shape's contours are scaled by scale (document pixels -> raster pixels) and lowered with cell origin
// (-GPU_SVG_ATLAS_PAD, content_h + GPU_SVG_ATLAS_PAD), so the y-up document space lands y-down in the padded cell.
// out_floats must hold the worst case (sum of shape segment_count) * GPU_GLYPH_LOWER_MAX_QUADS_PER_SEGMENT * 6
// floats; out_shapes must hold vector->shape_count records. scratch backs the per-shape scaled-segment copies.
// Shapes stay in document order. Frame global_alpha is NOT folded into the baked color - the atlas cell persists
// across frames, so global_alpha rides the per-frame white draw tint.
u32 gpu_svg_color_lower(struct FlArena* scratch, const struct FlVectorImage* vector, f32 scale, f32 content_h,
                        f32* out_floats, GpuColorShape* out_shapes, u32* out_shape_count);
