// Glyph rasterization: one compute dispatch per prepared glyph - one thread per cell texel
// looping the glyph's curve segments from an SSBO, accumulating winding/signed coverage, writing R8.
//
// The SSBO carries quadratic Beziers only (3 vec2 per segment: p0, control, p1) in padded-cell-local pixel
// coordinates, y-down; prepare_glyph lowers the shared outline's lines (control = midpoint) and cubics
// (split into quadratics) before upload, so the shader walks one segment kind. Per texel the shared
// curve_area kernel (coverage_kernel.glsl) walks each curve as CHORD_STEPS chords clipped to the texel's y
// band. Summed over the closed-contour soup the net is the fractional winding integral: |acc| is 1 deep
// inside, 0 outside, and an analytic AA ramp on edges of any orientation. Opposite-wound counters ('o')
// cancel to 0. Area accumulation is endpoint-exact, so shared segment endpoints can't crack or double-count;
// overlapping same-wound contours saturate via the final clamp.
//
// Every texel of the padded cell is written (the padding ring computes ~0 coverage), so cells never carry
// stale storage and the "zero in padding" contract holds without clearing the atlas texture.
#define CURVE_STRIDE 3u // float2 per quadratic segment: p0, control, p1
// Signature seam (shader_compat_metal.h): GL declares the curve SSBO, write image, and uniforms at global scope;
// MSL takes them as kernel arguments. The shared body below reads the same names on both.
#ifdef FL_METAL
kernel void glyph_main(device const float2* curve_pts [[buffer(BIND_GLYPH_CURVES)]],
                       constant uint2& cell_origin [[buffer(BIND_GLYPH_CELL_ORIGIN)]],
                       constant uint2& cell_size [[buffer(BIND_GLYPH_CELL_SIZE)]],
                       constant uint& seg_count [[buffer(BIND_GLYPH_SEG_COUNT)]],
                       texture2d<float, access::write> glyph_atlas [[texture(BIND_GLYPH_TEX_ATLAS)]],
                       uint3 _fl_global_id [[thread_position_in_grid]]) {
#else
LOCAL_SIZE_2D(8, 8);
BUFFER(Curves, float2, curve_pts, 0); // CURVE_STRIDE float2 per quadratic
IMAGE2D_WRITE(r8, glyph_atlas, 0);
UNIFORM(uint2, cell_origin); // the padded cell's texel position in the atlas
UNIFORM(uint2, cell_size);   // padded cell extent - every texel gets written
UNIFORM(uint, seg_count);
KERNEL(main) {
#endif
    uint2 t = gl_GlobalInvocationID.xy;
    if (t.x >= cell_size.x || t.y >= cell_size.y) {
        return;
    }
    float2 pc = float2(t) + 0.5; // texel center, cell-local
    float acc = 0.0;
    for (uint s = 0u; s < seg_count; s++) {
        acc += curve_area(curve_pts[s * CURVE_STRIDE + 0u] - pc, curve_pts[s * CURVE_STRIDE + 1u] - pc,
                          curve_pts[s * CURVE_STRIDE + 2u] - pc);
    }
    float coverage = clamp(abs(acc), 0.0, 1.0);
    WRITE_IMAGE(glyph_atlas, int2(cell_origin + t), float4(coverage, 0.0, 0.0, 0.0));
}
