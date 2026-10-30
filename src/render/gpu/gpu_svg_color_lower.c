#include "gpu_svg_color_lower.h"
#include <core/arena.h>
#include "gpu_glyph_lower.h"
#include "gpu_svg_atlas.h"
#include <ui/vector_image.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Bake a shape's solid fill and opacity into one premultiplied RGBA8 word - R in the low byte (GLSL
// unpackUnorm4x8 order), rounded to nearest. Frame global_alpha is not folded in.
static u32 bake_premultiplied(FlColor fill, f32 opacity) {
    f32 o = opacity < 0.0f ? 0.0f : (opacity > 1.0f ? 1.0f : opacity);
    f32 a = (f32)fill.a / 255.0f * o; // final alpha as a fraction
    u32 r = (u32)((f32)fill.r * a + 0.5f);
    u32 g = (u32)((f32)fill.g * a + 0.5f);
    u32 b = (u32)((f32)fill.b * a + 0.5f);
    u32 ai = (u32)(a * 255.0f + 0.5f);
    return r | (g << 8) | (b << 16) | (ai << 24);
}

u32 gpu_svg_color_lower(struct FlArena* scratch, const FlVectorImage* vector, f32 scale, f32 content_h, f32* out_floats,
                        GpuColorShape* out_shapes, u32* out_shape_count) {
    // Cell origin: cell x = p.x + PAD, cell y = (content_h - p.y) + PAD (y-up document -> y-down cell), expressed
    // as gpu_glyph_lower_segments' origin (cell_x = p.x - origin_x, cell_y = origin_y - p.y).
    f32 origin_x = -(f32)GPU_SVG_ATLAS_PAD;
    f32 origin_y = content_h + (f32)GPU_SVG_ATLAS_PAD;

    u32 total_quads = 0;
    for (u32 s = 0; s < vector->shape_count; s++) {
        const FlVectorShape* shape = &vector->shapes[s];
        u32 quad_start = total_quads;
        u32 quad_count = 0;
        if (shape->segment_count > 0) {
            // Scale the shape's contours into raster pixel space, then lower the soup to quadratics through the
            // shared glyph path. Each shape lowers independently so its quads are contiguous - the color shader
            // walks [quad_start, quad_start + quad_count) per shape.
            FlGlyphSegment* scaled = arena_alloc_array(scratch, FlGlyphSegment, shape->segment_count);
            for (u32 i = 0; i < shape->segment_count; i++) {
                FlGlyphSegment seg = shape->segments[i];
                for (u32 p = 0; p < 4; p++) {
                    seg.pts[p].x *= scale;
                    seg.pts[p].y *= scale;
                }
                scaled[i] = seg;
            }
            quad_count = gpu_glyph_lower_segments(scaled, shape->segment_count, origin_x, origin_y,
                                                  out_floats + (size_t)total_quads * 6);
        }
        out_shapes[s] = (GpuColorShape) {
            .quad_start = quad_start,
            .quad_count = quad_count,
            .color = bake_premultiplied(shape->fill, shape->opacity),
            .fill_rule = shape->fill_rule,
        };
        total_quads += quad_count;
    }

    *out_shape_count = vector->shape_count;
    return total_quads;
}
