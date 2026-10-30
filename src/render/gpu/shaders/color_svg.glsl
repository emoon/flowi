// Multi-color SVG rasterization: one compute dispatch per prepared multi-color icon - one thread per cell
// texel compositing the icon's shapes in document order (painter's algorithm) into an RGBA8 cell, the color
// generalization of glyph.glsl.
//
// The Curves SSBO (binding 0) holds every shape's lowered quadratics concatenated; the Shapes SSBO (binding 1)
// holds one uvec4 per shape - (quad_start, quad_count, premultiplied color, fill rule) - in document order
// (gpu_svg_color_lower). Per texel, each shape accumulates signed-area coverage over its own quad range via the
// shared curve_area kernel, folds its fill rule (nonzero: clamp(|acc|); even-odd: the mod-2 triangle wave),
// then over-blends its premultiplied color * coverage into a register accumulator. The single imageStore writes
// premultiplied RGBA - matching the image draw path's premultiplied-tint blend. No image read-modify-write, no
// barriers. Every padded texel is written (a shape with no coverage there leaves acc unchanged, so the ring
// stays transparent - the "zero in padding" contract without clearing the texture).
#define CURVE_STRIDE 3u // float2 per quadratic segment: p0, control, p1
// Signature seam (shader_compat_metal.h): GL declares the two SSBOs, the write image, and the cell uniforms at
// global scope; MSL takes them as kernel arguments. The shared body below reads the same names on both, and the
// even-odd fold's mod is reconciled there too - MSL has no GLSL mod.
#ifdef FL_METAL
kernel void color_svg_main(device const float2* curve_pts [[buffer(BIND_COLOR_SVG_CURVES)]],
                           device const uint4* shapes [[buffer(BIND_COLOR_SVG_SHAPES)]],
                           constant uint2& cell_origin [[buffer(BIND_COLOR_SVG_CELL_ORIGIN)]],
                           constant uint2& cell_size [[buffer(BIND_COLOR_SVG_CELL_SIZE)]],
                           constant uint& shape_count [[buffer(BIND_COLOR_SVG_SHAPE_COUNT)]],
                           texture2d<float, access::write> color_atlas [[texture(BIND_COLOR_SVG_TEX_ATLAS)]],
                           uint3 _fl_global_id [[thread_position_in_grid]]) {
#else
LOCAL_SIZE_2D(8, 8);
BUFFER(Curves, float2, curve_pts, 0);
BUFFER(Shapes, uint4, shapes, 1); // quad_start, quad_count, color, rule
IMAGE2D_WRITE(rgba8, color_atlas, 0);
UNIFORM(uint2, cell_origin); // the padded cell's texel position in the atlas
UNIFORM(uint2, cell_size);   // padded cell extent - every texel gets written
UNIFORM(uint, shape_count);
KERNEL(main) {
#endif
    uint2 t = gl_GlobalInvocationID.xy;
    if (t.x >= cell_size.x || t.y >= cell_size.y) {
        return;
    }
    float2 pc = float2(t) + 0.5; // texel center, cell-local
    float4 acc = float4(0.0); // premultiplied, painter's order
    for (uint si = 0u; si < shape_count; si++) {
        uint4 shape = shapes[si];
        float area = 0.0;
        uint q_end = shape.x + shape.y;
        for (uint s = shape.x; s < q_end; s++) {
            area += curve_area(curve_pts[s * CURVE_STRIDE + 0u] - pc, curve_pts[s * CURVE_STRIDE + 1u] - pc,
                               curve_pts[s * CURVE_STRIDE + 2u] - pc);
        }
        // Fill rule fold: even-odd is the mod-2 triangle wave (a doubly-wound hole -> 0), nonzero clamps
        // |area|. mod handles a negative winding (the y flip) symmetrically.
        float coverage = shape.w == 1u ? 1.0 - abs(1.0 - mod(area, 2.0)) : clamp(abs(area), 0.0, 1.0);
        float4 src = unpackUnorm4x8(shape.z) * coverage; // color is already premultiplied (opacity baked in)
        acc = src + acc * (1.0 - src.a); // over-blend in document order
    }
    WRITE_IMAGE(color_atlas, int2(cell_origin + t), acc);
}
