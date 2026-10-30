// Pass 2: raster - one workgroup per tile, one thread per pixel, blending the tile's classified commands.
//
// The tile's classified index list is cooperatively staged through shared memory: thread i loads list entry i,
// which covers the whole list since workgroup size equals list capacity. Only the indices are staged, so the
// blend loop's command fetch is workgroup-uniform. Blending runs in registers, premultiplied, over a transparent
// target, so the stored alpha is real coverage for the host's composite; the only image access is the final
// WRITE_IMAGE store.
//
// Every kind's coverage comes from a signed distance field evaluated at the pixel center and AA'd as
// clamp(0.5 - d), which is exact analytic pixel coverage on a straight axis-aligned edge and falls off smoothly
// around a curve. The command's clip rect cuts coverage the same way and multiplies in. A
// fill's color is the bilinear blend of its four corner colors at the pixel center, so four equal corners
// reproduce a solid color exactly. Radii, wedge frames, and inner-ring geometry arrive pre-clamped and
// pre-rotated from the translator; the per-kind fields below document what each one carries.
//
// debug_mode (gpu_debug.h) short-circuits the blend loop with a per-tile visualization of the classification
// data. The branch sits after the staging barrier and is uniform across the dispatch.
#define DEBUG_TILE_HEATMAP 1u // GpuDebug_TileHeatMap

// GL side of the signature seam: the SSBOs, samplers, write image, uniforms, and threadgroup index list at global
// scope. On Metal these become the raster_main kernel arguments below, every atlas a texture argument and the
// per-slot images a texture array. The shared body is identical on both backends.
#ifndef FL_METAL
LOCAL_SIZE_2D(TILE_SIZE, TILE_SIZE);
BUFFER(DrawCmds, DrawCmd, cmds, 0);
BUFFER(TileLists, uint, tile_lists, 2);
BUFFER(ClipRects, float4, clips, 3); // x0 y0 x1 y1
SAMPLER2D(atlas_linear);  // the image atlas on two units, one per filter mode
SAMPLER2D(atlas_nearest);
SAMPLER2D(glyph_atlas);   // the R8 glyph-coverage atlas, linear-filtered
SAMPLER2D(svg_atlas);     // the R8 SVG vector-icon coverage atlas linear-filtered
SAMPLER2D(svg_color_atlas); // the RGBA8 multi-color SVG atlas, linear-filtered
// Large/dynamic images: one texture per sampler slot (fixed units; the slot's sampler object carries
// the filter mode), with the reciprocal texture extent alongside for uv normalization.
SAMPLER2D_ARRAY(slot_tex, IMAGE_SLOT_COUNT);
UNIFORM_ARRAY(float2, slot_inv_size, IMAGE_SLOT_COUNT);
IMAGE2D_WRITE(rgba8, target, 0);
UNIFORM(uint, tile_cols);
UNIFORM(uint2, screen_size);
UNIFORM(uint, debug_mode); // GpuDebugMode: 0 = off, 1 = tile heat-map
SHARED(uint, sh_idx, TILE_CAPACITY);
#endif
// Rounded-rect signed distance at point p: rect is x, y, w, h; radius is per-corner TL TR BL BR,
// selected by quadrant (y-down: negative q.y is the top half). Radii must be pre-clamped to
// [0, half the smaller dimension] so adjacent corners never overlap. Shared by fills and both of a
// border's edges; coverage = clamp(0.5 - d) equals exact analytic pixel coverage on a straight edge.
float rounded_rect_dist(float2 p, float4 rect, float4 radius) {
    float2 half_size = rect.zw * 0.5;
    float2 q = p - rect.xy - half_size;
    float rad = q.x < 0.0 ? (q.y < 0.0 ? radius.x : radius.z) : (q.y < 0.0 ? radius.y : radius.w);
    float2 e = abs(q) - half_size + float2(rad);
    return min(max(e.x, e.y), 0.0) + length(max(e, float2(0.0))) - rad;
}
// Per-kind signed distance to a shape's outline. Each shape's own coverage and its effect (drop shadow /
// glow) read the same field, so the effect always tracks the real outline rather than a box approximation.
// Rect / image / slot image: images carry their source texel rect in the radius words, so they are sharp
// boxes (radius 0); a fill rect uses its per-corner radii.
float rect_dist(DrawCmd cmd, float2 p) {
    return rounded_rect_dist(p, cmd.rect, cmd.kind == KIND_RECT ? cmd.radius : float4(0.0));
}
// Border: the outer rounded rect, and the inner one derived here from the per-side widths (the IR
// contract: no pre-split inner radius). The band is outer minus inner; the silhouette is their ring.
float border_outer_dist(DrawCmd cmd, float2 p) { return rounded_rect_dist(p, cmd.rect, cmd.radius); }
float border_inner_dist(DrawCmd cmd, float2 p) {
    float4 w = float4(uintBitsToFloat(cmd.color.y), uintBitsToFloat(cmd.color.z),
                  uintBitsToFloat(cmd.color.w), uintBitsToFloat(cmd.aux)); // l, r, t, b
    float4 inner = float4(cmd.rect.xy + w.xz, cmd.rect.zw - w.xz - w.yw);
    float4 iradius = clamp(cmd.radius - max(w.xyxy, w.zzww), 0.0, 0.5 * min(inner.z, inner.w));
    return rounded_rect_dist(p, inner, iradius);
}
// Arc (ring sector): rect = (cx, cy, outer, inner); radius.xy = the sweep bisector's cos/sin,
// radius.zw = the half-aperture's sin/cos (knob convention pre-converted by the translator).
// Work in a y-up frame rotated so the bisector points up; folding to |x| makes the two wedge edges one.
float arc_dist(DrawCmd cmd, float2 p) {
    float2 rel = p - cmd.rect.xy;
    float2 q = float2(rel.x, -rel.y);
    float2 t = float2(cmd.radius.x * q.x - cmd.radius.y * q.y,
                  cmd.radius.y * q.x + cmd.radius.x * q.y);
    t.x = abs(t.x);
    float outer_r = cmd.rect.z;
    float inner_r = cmd.rect.w;
    float len = length(t);
    // Annulus SDF; a filled sector (inner 0) is the plain disc, avoiding the degenerate annulus center.
    float d = inner_r > 0.0 ? abs(len - 0.5 * (outer_r + inner_r)) - 0.5 * (outer_r - inner_r)
                            : len - outer_r;
    if ((cmd.flags & ARC_FULL) == 0u) { // not a full circle: cut the aperture wedge
        float2 ca = cmd.radius.zw;
        // Distance to the cap edge (the radial segment inner..outer at the aperture), signed positive
        // outside the wedge; intersecting (max) keeps butt caps sharp.
        float m = length(t - ca * clamp(dot(t, ca), inner_r, outer_r));
        d = max(d, m * sign(ca.y * t.x - ca.x * t.y));
        if ((cmd.flags & ARC_ROUND_CAPS) != 0u) {
            // Round caps: union (min) with the end-cap disc on the centerline.
            float cap = length(t - ca * (0.5 * (outer_r + inner_r))) - 0.5 * (outer_r - inner_r);
            d = min(d, cap);
        }
    }
    return d;
}
// Circle: rect = (cx, cy, radius, border_width). The disc; the inward border ring is the shape's own
// concern, not the silhouette's.
float circle_dist(DrawCmd cmd, float2 p) { return length(p - cmd.rect.xy) - cmd.rect.z; }
// Diamond: rect = (cx, cy, rx, ry) half-extents; radius.x = border width.
float diamond_dist(DrawCmd cmd, float2 p) {
    float2 rel = p - cmd.rect.xy;
    float2 b = cmd.rect.zw; // half-extents
    float2 ap = abs(rel);
    float h = clamp((b.x * (b.x - 2.0 * ap.x) - b.y * (b.y - 2.0 * ap.y)) / dot(b, b), -1.0, 1.0);
    return length(ap - 0.5 * b * float2(1.0 - h, 1.0 + h)) *
           sign(ap.x * b.y + ap.y * b.x - b.x * b.y);
}
// Polyline stroke capsule: rect = (p0, p1), radius.x = half-width. The segment distance field minus the
// half-width - round caps and joins fall out of the clamped projection (segment_dist).
float poly_capsule_dist(DrawCmd cmd, float2 p) { return segment_dist(p, cmd.rect.xy, cmd.rect.zw) - cmd.radius.x; }
#define TAU 6.28318530717959 // one full turn: the arc length a circle's ring dash pattern repeats over
// Dashes: a border band and a circle's border ring can carry a repeating dash/gap pattern, two lengths packed
// into the flags word's high bits (kind_defs.glsl). A zero dash length is a solid stroke. The pattern modulates
// the ring coverage only, never silhouette_dist, so a dashed shape's drop shadow is cast from its whole outline.
float2 dash_lengths(uint flags) {
    return float2(float((flags >> DASH_SHIFT) & DASH_MASK), float((flags >> GAP_SHIFT) & DASH_MASK)) *
           DASH_PX_PER_UNIT;
}
// Dash coverage at distance s along a run of length run_len. The period is rescaled so a whole number of dashes
// fits the run exactly, the dash keeping its share, so the run closes on a pattern boundary and only the
// dash:gap ratio survives the packed lengths' 1/16px quantization. AA'd as clamp(0.5 - d) like every other edge
// here, d being the signed distance along the run to the nearest dash, both sides of the wrap included.
float dash_coverage(float2 dash_gap, float s, float run_len) {
    float period = dash_gap.x + dash_gap.y;
    float scaled = run_len / max(floor(run_len / period + 0.5), 1.0);
    float dash = dash_gap.x * scaled / period;
    float u = mod(s, scaled);
    float d = u < dash ? -min(u, dash - u) : min(u - dash, scaled - u);
    return clamp(0.5 - d, 0.0, 1.0);
}
// Rect border: the dash runs along whichever edge the pixel is nearest, each edge rescaled to close on its own
// length. The runs meeting at a corner are not phase-continuous.
float border_dash_coverage(DrawCmd cmd, float2 p) {
    float2 dash_gap = dash_lengths(cmd.flags);
    if (dash_gap.x <= 0.0) {
        return 1.0;
    }
    float2 q = p - cmd.rect.xy;
    float2 to_far = cmd.rect.zw - q;
    // Nearer a left/right edge than a top/bottom one: the run is vertical, so it goes along the height.
    bool vertical = min(q.x, to_far.x) < min(q.y, to_far.y);
    return dash_coverage(dash_gap, vertical ? q.y : q.x, vertical ? cmd.rect.w : cmd.rect.z);
}
// Circle: the dash runs along the ring's centerline, so the pattern closes on that circumference rather than
// on either of the ring's edges. rect = (cx, cy, radius, border_width), and the translator only packs a
// pattern when the ring is real (0 < border_width < radius), so the mid radius here is positive.
float circle_dash_coverage(DrawCmd cmd, float2 p) {
    float2 dash_gap = dash_lengths(cmd.flags);
    if (dash_gap.x <= 0.0) {
        return 1.0;
    }
    float mid_radius = cmd.rect.z - 0.5 * cmd.rect.w;
    float2 rel = p - cmd.rect.xy;
    return dash_coverage(dash_gap, (atan(rel.y, rel.x) + 0.5 * TAU) * mid_radius, TAU * mid_radius);
}
// The shape's outermost outline, dispatched by kind - what a drop shadow or glow is cast from.
float silhouette_dist(DrawCmd cmd, float2 p) {
    if (cmd.kind == KIND_BORDER) { return max(border_outer_dist(cmd, p), -border_inner_dist(cmd, p)); }
    if (cmd.kind == KIND_ARC) { return arc_dist(cmd, p); }
    if (cmd.kind == KIND_CIRCLE) { return circle_dist(cmd, p); }
    if (cmd.kind == KIND_DIAMOND) { return diamond_dist(cmd, p); }
    if (cmd.kind == KIND_POLYLINE) {
        // Only a stroke capsule casts an effect silhouette; a fill trapezoid casts none (a far distance,
        // so zero effect coverage) - the stroke over the fill carries the polyline's shadow/glow.
        return (cmd.flags & POLY_STROKE) != 0u ? poly_capsule_dist(cmd, p) : 1e9;
    }
    return rect_dist(cmd, p);
}
// Signature seam, the raster half: on Metal the resources bind as kernel arguments and the tile /
// pixel / list indices arrive as [[threadgroup_position_in_grid]] / [[thread_position_in_grid]] /
// [[thread_index_in_threadgroup]] (mapped to the gl_* names the body reads by shader_compat_metal.h), and the
// threadgroup index list is a function-local declaration. The R8 glyph coverage atlas, the RGBA8 image atlas,
// and the R8 + RGBA8 SVG vector-icon atlases bind as sampled texture
// arguments alongside function-local linear/nearest samplers the image/glyph/SVG sampling reads; the per-slot
// large/dynamic images bind as a texture array (slot_tex) with their reciprocal extents + filter modes in two
// constant buffers. The rect/border/SDF/glyph/image/SVG/slot blend loop below is shared verbatim.
#ifdef FL_METAL
kernel void raster_main(device const DrawCmd* cmds [[buffer(BIND_RASTER_CMDS)]],
                        device const uint* tile_lists [[buffer(BIND_RASTER_TILE_LISTS)]],
                        device const float4* clips [[buffer(BIND_RASTER_CLIPS)]],
                        texture2d<float, access::write> target [[texture(BIND_RASTER_TEX_TARGET)]],
                        texture2d<float, access::sample> glyph_atlas [[texture(BIND_RASTER_TEX_GLYPH)]],
                        texture2d<float, access::sample> image_atlas [[texture(BIND_RASTER_TEX_IMAGE)]],
                        texture2d<float, access::sample> svg_atlas [[texture(BIND_RASTER_TEX_SVG)]],
                        texture2d<float, access::sample> svg_color_atlas [[texture(BIND_RASTER_TEX_SVG_COLOR)]],
                        array<texture2d<float, access::sample>, IMAGE_SLOT_COUNT> slot_tex
                            [[texture(BIND_RASTER_TEX_SLOT0)]],
                        constant uint& tile_cols [[buffer(BIND_RASTER_TILE_COLS)]],
                        constant uint2& screen_size [[buffer(BIND_RASTER_SCREEN_SIZE)]],
                        constant uint& debug_mode [[buffer(BIND_RASTER_DEBUG_MODE)]],
                        constant float2* slot_inv_size [[buffer(BIND_RASTER_SLOT_INV_SIZE)]],
                        constant uint* slot_filter [[buffer(BIND_RASTER_SLOT_FILTER)]],
                        uint3 _fl_group_id [[threadgroup_position_in_grid]],
                        uint _fl_local_index [[thread_index_in_threadgroup]],
                        uint3 _fl_global_id [[thread_position_in_grid]]) {
    SHARED(uint, sh_idx, TILE_CAPACITY);
    constexpr sampler _fl_sampler_linear(mag_filter::linear, min_filter::linear, coord::normalized,
                                         address::clamp_to_edge);
    // The image atlas's nearest filter mode: on GL two sampler objects bind to the atlas texture, on Metal the
    // one texture argument pairs with these two function-local samplers (the combined-sampler seam).
    constexpr sampler _fl_sampler_nearest(mag_filter::nearest, min_filter::nearest, coord::normalized,
                                          address::clamp_to_edge);
#else
KERNEL(main) {
#endif
    uint2 tile = gl_WorkGroupID.xy;
    uint base = (tile.y * tile_cols + tile.x) * TILE_STRIDE;
    uint count = tile_lists[base];
    uint lidx = gl_LocalInvocationIndex;
    // Unconditional (branchless): the list region is always allocated at full capacity, so lanes past
    // count just stage words the blend loop never reads.
    sh_idx[lidx] = tile_lists[base + TILE_HEADER + lidx];
    BARRIER();
    uint2 px = gl_GlobalInvocationID.xy;
    if (px.x >= screen_size.x || px.y >= screen_size.y) {
        return;
    }
    if (debug_mode == DEBUG_TILE_HEATMAP) {
        // Tile heat-map: command count as a green->red ramp, empty tiles transparent, overflow magenta.
        uint overflow = tile_lists[base + 1u];
        float4 heat = float4(0.0);
        if (overflow > 0u) {
            heat = float4(1.0, 0.0, 1.0, 1.0);
        } else if (count > 0u) {
            float t = float(count) / float(TILE_CAPACITY);
            heat = float4(t, 1.0 - t, 0.0, 1.0);
        }
        WRITE_IMAGE(target, int2(px), heat);
        return;
    }
    float2 pc = float2(px) + 0.5; // pixel center
    float4 acc = float4(0.0); // premultiplied
    for (uint k = 0u; k < count; k++) {
        DrawCmd cmd = cmds[sh_idx[k]]; // k is workgroup-uniform, so this is a broadcast fetch
        float4 src = float4(0.0); // premultiplied source color with the shape's coverage folded in (0 = no
                                  // contribution until a kind branch writes it)
        if (cmd.kind <= KIND_IMAGE || cmd.kind == KIND_SLOT_IMAGE || cmd.kind == KIND_GLYPH ||
            cmd.kind == KIND_SVG || cmd.kind == KIND_SVG_COLOR) {
            float4 r = cmd.rect;
            // Images (atlas and slot) and glyphs are sharp boxes (radius 0) - their radius words carry
            // the source texel rect instead (handled by rect_dist).
            float d = rect_dist(cmd, pc);
            float coverage = clamp(0.5 - d, 0.0, 1.0);
            if (cmd.kind != KIND_RECT) {
                // The dst-relative position maps into the source texel rect, clamped half a texel inside so
                // linear filtering never reads a neighbor (an adjacent atlas cell, or past a slot texture's
                // sub-rect edge). Sampling uses an explicit LOD. Both storage families hold premultiplied texels
                // (alpha-coverage sources as white x coverage), and the tint arrives premultiplied, so every
                // image/glyph mode and tint modulation is the one multiply that folds src below.
                float2 t = (pc - r.xy) / r.zw;
                float2 texel = mix(cmd.radius.xy, cmd.radius.zw, t);
                float2 lo = cmd.radius.xy + 0.5;
                float2 hi = max(lo, cmd.radius.zw - 0.5);
                float2 tc = clamp(texel, lo, hi);
                float4 s = float4(0.0);
                if (cmd.kind == KIND_GLYPH) {
                    // R8 glyph coverage read as white x coverage (premultiplied), so the shared multiply
                    // below colorizes it with the run's premultiplied tint - the alpha-coverage contract.
                    // The linear/nearest filter selection (image atlas and slot images) is per-backend.
                    s = float4(textureLod(glyph_atlas, tc / float(GLYPH_ATLAS_SIZE), 0.0).r);
                } else if (cmd.kind == KIND_IMAGE) {
                    // Image atlas (RGBA8): color texels, or an alpha-coverage source stored as white x alpha -
                    // both premultiplied, so the shared multiply below folds tint and coverage identically. GL
                    // binds two sampler objects to the atlas texture; Metal pairs the one texture argument with
                    // two function-local samplers.
                    float2 uv = tc / float(IMAGE_ATLAS_SIZE);
#ifdef FL_METAL
                    s = (cmd.flags & IMAGE_NEAREST) != 0u ? image_atlas.sample(_fl_sampler_nearest, uv, level(0.0))
                                                          : image_atlas.sample(_fl_sampler_linear, uv, level(0.0));
#else
                    s = (cmd.flags & IMAGE_NEAREST) != 0u ? textureLod(atlas_nearest, uv, 0.0)
                                                          : textureLod(atlas_linear, uv, 0.0);
#endif
                } else if (cmd.kind == KIND_SVG) {
                    // R8 SVG vector-icon coverage, same alpha-coverage contract as a glyph: white x
                    // coverage colorized below by the icon's premultiplied tint. This and the color cell below are
                    // linear-filtered.
                    s = float4(textureLod(svg_atlas, tc / float(SVG_ATLAS_SIZE), 0.0).r);
                } else if (cmd.kind == KIND_SVG_COLOR) {
                    // RGBA8 multi-color SVG cell: the baked premultiplied color, modulated below by the
                    // white tint (so frame global_alpha still applies).
                    s = textureLod(svg_color_atlas, tc / float(SVG_COLOR_ATLAS_SIZE), 0.0);
                }
                else if (cmd.kind == KIND_SLOT_IMAGE) {
                    // Slot image (large statics / dynamic buffers, each on its own texture): cmd.aux is the slot
                    // index - workgroup-uniform (the broadcast cmd fetch above), so this sampler-array index is
                    // dynamically uniform. The reciprocal texture extent normalizes the texel coordinate to
                    // [0,1]. GL binds the filter as the slot's sampler object; Metal selects between two
                    // function-local samplers by the slot's filter value.
                    float2 slot_uv = tc * slot_inv_size[cmd.aux];
#ifdef FL_METAL
                    s = slot_filter[cmd.aux] != 0u ? slot_tex[cmd.aux].sample(_fl_sampler_nearest, slot_uv, level(0.0))
                                                   : slot_tex[cmd.aux].sample(_fl_sampler_linear, slot_uv, level(0.0));
#else
                    s = textureLod(slot_tex[cmd.aux], slot_uv, 0.0);
#endif
                }
                src = s * unpackUnorm4x8(cmd.color.x) * coverage;
            } else {
                // Bilinear corner-color blend at the pixel center; four equal corners stay exact.
                float2 uv = clamp((pc - r.xy) / r.zw, 0.0, 1.0);
                float4 ctop = mix(unpackUnorm4x8(cmd.color.x), unpackUnorm4x8(cmd.color.y), uv.x);
                float4 cbot = mix(unpackUnorm4x8(cmd.color.z), unpackUnorm4x8(cmd.color.w), uv.x);
                float4 col = mix(ctop, cbot, uv.y);
                src = float4(col.rgb * col.a, col.a) * coverage;
            }
        } else if (cmd.kind == KIND_BORDER) {
            // Border: outer coverage from the same rounded-rect SDF as a fill; the inner rounded
            // rect is derived from the outer radius and the per-side widths (the IR contract: no
            // pre-split inner radius). The band is outer minus inner coverage, AA'd at both edges; a
            // zero-width side puts the inner edge on the outer one, so nothing lands there. The
            // translator guarantees a non-empty inner rect (a border consuming the rect arrives as a
            // plain fill).
            float outer_cov = clamp(0.5 - border_outer_dist(cmd, pc), 0.0, 1.0);
            float inner_cov = clamp(0.5 - border_inner_dist(cmd, pc), 0.0, 1.0);
            float4 col = unpackUnorm4x8(cmd.color.x);
            src = float4(col.rgb * col.a, col.a) * ((outer_cov - inner_cov) * border_dash_coverage(cmd, pc));
        } else if (cmd.kind == KIND_ARC) {
            // Arc (ring sector): the annulus-and-wedge SDF, shared with the silhouette path.
            float d = arc_dist(cmd, pc);
            float4 col = unpackUnorm4x8(cmd.color.x);
            src = float4(col.rgb * col.a, col.a) * clamp(0.5 - d, 0.0, 1.0);
        } else if (cmd.kind == KIND_POLYLINE) {
            // One polyline segment: rect = (p0, p1), radius = (half-width, intensity0, intensity1,
            // baseline_y), color.x the stroke or fill color. POLY_STROKE selects a stroke capsule or a
            // fill-to-baseline trapezoid; intensity (lerped along the segment) scales the alpha.
            float2 a = cmd.rect.xy;
            float2 b = cmd.rect.zw;
            float cov = 0.0;
            float intensity = 0.0;
            if ((cmd.flags & POLY_STROKE) != 0u) {
                // Stroke capsule: the segment distance field minus the half-width, AA'd as clamp(0.5 - d).
                // h is the projection along the segment - round caps/joins fall out, and the per-endpoint
                // intensities lerp by it.
                float d = poly_capsule_dist(cmd, pc);
                cov = clamp(0.5 - d, 0.0, 1.0);
                float2 ba = b - a;
                float bb = dot(ba, ba);
                float h = bb > 0.0 ? clamp(dot(pc - a, ba) / bb, 0.0, 1.0) : 0.0;
                intensity = mix(cmd.radius.y, cmd.radius.z, h);
            } else {
                // Fill-to-baseline trapezoid under the segment: a hard x-clip to the segment's x-span so
                // neighbouring trapezoids abut seamlessly (no AA on the shared vertical edges), AA only on
                // the curve edge and the baseline. Intended for x-monotonic curves; the translator drops
                // vertical (zero x-span) segments, so b.x != a.x here.
                float t = (pc.x - a.x) / (b.x - a.x);
                if (t >= 0.0 && t < 1.0) {
                    float line_y = mix(a.y, b.y, t);
                    float top = min(line_y, cmd.radius.w);
                    float bot = max(line_y, cmd.radius.w);
                    cov = clamp(pc.y - top + 0.5, 0.0, 1.0) * clamp(bot - pc.y + 0.5, 0.0, 1.0);
                    intensity = mix(cmd.radius.y, cmd.radius.z, t);
                }
            }
            float4 col = unpackUnorm4x8(cmd.color.x);
            src = float4(col.rgb * col.a, col.a) * (cov * clamp(intensity, 0.0, 1.0));
        } else {
            // Circle (rect = cx, cy, radius, border_width) / diamond (rect = cx, cy, rx, ry;
            // radius.x = border_width): the centered SDF (shared with the silhouette path) with the
            // optional inward border ring composited as two coverages - outer minus inner is the ring,
            // AA'd at both edges. The translator guarantees border_width < the inradius, so the fill
            // region never degenerates.
            float d;
            float bw;
            float dash_cov = 1.0; // only a circle's ring carries a dash pattern; a diamond's is always solid
            if (cmd.kind == KIND_CIRCLE) {
                d = circle_dist(cmd, pc);
                bw = cmd.rect.w;
                dash_cov = circle_dash_coverage(cmd, pc);
            } else {
                d = diamond_dist(cmd, pc);
                bw = cmd.radius.x;
            }
            float outer_cov = clamp(0.5 - d, 0.0, 1.0);
            float inner_cov = clamp(0.5 - d - bw, 0.0, 1.0);
            float4 fc = unpackUnorm4x8(cmd.color.x);
            float4 bc = unpackUnorm4x8(cmd.color.y);
            src = float4(fc.rgb * fc.a, fc.a) * inner_cov +
                  float4(bc.rgb * bc.a, bc.a) * ((outer_cov - inner_cov) * dash_cov);
        }
        // Clip cut: the clip rect's own box distance field, AA'd the same way; both the shape and any
        // effect are cut by it.
        float4 cr = clips[cmd.clip];
        float2 cq = abs(pc - (cr.xy + cr.zw) * 0.5) - (cr.zw - cr.xy) * 0.5;
        float cd = min(max(cq.x, cq.y), 0.0) + length(max(cq, float2(0.0)));
        float clip_cov = clamp(0.5 - cd, 0.0, 1.0);
        // Effect under the shape (bits 8..9 of flags: 1 = drop shadow, 2 = glow). The shadow casts the
        // silhouette offset + blurred and blends below; the glow casts it centered + blurred and adds.
        // The shape's own src lands on top unchanged, so a fully covered interior pixel is identical with
        // or without the effect - the effect never disturbs the primitive's edge AA.
        uint efk = (cmd.flags >> EFFECT_SHIFT) & EFFECT_MASK;
        if (efk != 0u) {
            float2 off = efk == EFFECT_SHADOW ? float2(uintBitsToFloat(cmd.effect.x), uintBitsToFloat(cmd.effect.y))
                                            : float2(0.0); // glow is centered
            float blur = uintBitsToFloat(cmd.effect.z);
            // Soft falloff from the silhouette SDF: a ramp of half-width max(blur, 0.5), so blur 0 is the
            // shape's own AA edge and larger blur spreads monotonically outward.
            float hw = max(blur, 0.5);
            float ecov = clamp(0.5 - 0.5 * silhouette_dist(cmd, pc - off) / hw, 0.0, 1.0);
            float4 ec = unpackUnorm4x8(cmd.effect.w);
            float4 efx = float4(ec.rgb * ec.a, ec.a) * ecov * clip_cov;
            acc = efk == EFFECT_SHADOW ? efx + acc * (1.0 - efx.a) // drop shadow: over background, under shape
                                       : acc + efx;                // glow: additive
        }
        src *= clip_cov;
        acc = src + acc * (1.0 - src.a);
    }
    WRITE_IMAGE(target, int2(px), acc);
}
