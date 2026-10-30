// Wallace signed-area coverage of one quadratic Bezier, the kernel shared by the monochrome glyph/SVG raster
// (glyph.glsl) and the multi-color SVG raster (color_svg.glsl). Points arrive pre-translated so the texel
// center is the origin (callers pass curve_pts[..] - pc). The whole-curve reject off the control hull -
// outside the texel's y band, or entirely to its left - contributes nothing; otherwise the curve is walked as
// CHORD_STEPS uniform-parameter chords, each clipped to the texel's y band, and the band's signed height times
// the fraction of the texel row left of the chord is accumulated. Summed over a closed-contour soup the net is
// the fractional winding integral the callers fold per fill rule. Endpoint-exact, so shared segment endpoints
// neither crack nor double-count.
#define CHORD_STEPS 8u // uniform-parameter chords each curve is walked as
float curve_area(float2 p0, float2 c, float2 p1) {
    if (min(p0.y, min(c.y, p1.y)) > 0.5 || max(p0.y, max(c.y, p1.y)) < -0.5 ||
        max(p0.x, max(c.x, p1.x)) < -0.5) {
        return 0.0;
    }
    float acc = 0.0;
    float2 prev = p0;
    for (uint i = 1u; i <= CHORD_STEPS; i++) {
        float tt = float(i) / float(CHORD_STEPS);
        float2 q = mix(mix(p0, c, tt), mix(c, p1, tt), tt);
        float dy = q.y - prev.y;
        if (abs(dy) > 1e-6) {
            // Clip the chord to the texel's y band; b - a keeps the chord's direction so the band stays
            // winding-signed for descending chords (the clip params are ordered before clamping).
            float s0 = (-0.5 - prev.y) / dy;
            float s1 = (0.5 - prev.y) / dy;
            float2 a = mix(prev, q, clamp(min(s0, s1), 0.0, 1.0));
            float2 b = mix(prev, q, clamp(max(s0, s1), 0.0, 1.0));
            acc += clamp(0.5 * (a.x + b.x) + 0.5, 0.0, 1.0) * (b.y - a.y);
        }
        prev = q;
    }
    return acc;
}
