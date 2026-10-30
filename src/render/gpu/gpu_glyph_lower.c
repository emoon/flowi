#include "gpu_glyph_lower.h"
#include <ui/font_cache.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct GlyphQuadEmit {
    f32* floats; // 6 per quadratic: p0, control, p1
    u32 quad_count;
} GlyphQuadEmit;

static void glyph_emit_quad(GlyphQuadEmit* e, Vec2 p0, Vec2 c, Vec2 p1) {
    f32* dst = e->floats + (size_t)e->quad_count * 6;
    dst[0] = p0.x;
    dst[1] = p0.y;
    dst[2] = c.x;
    dst[3] = c.y;
    dst[4] = p1.x;
    dst[5] = p1.y;
    e->quad_count++;
}

static Vec2 vec2_mid(Vec2 a, Vec2 b) {
    return (Vec2) { (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f };
}

// The single quadratic nearest a cubic: control = (3(c1 + c2) - p0 - p3) / 4.
static void glyph_emit_cubic(GlyphQuadEmit* e, Vec2 p0, Vec2 c1, Vec2 c2, Vec2 p3, int depth) {
    if (depth == 0) {
        Vec2 c = { (3.0f * (c1.x + c2.x) - p0.x - p3.x) * 0.25f, (3.0f * (c1.y + c2.y) - p0.y - p3.y) * 0.25f };
        glyph_emit_quad(e, p0, c, p3);
        return;
    }
    Vec2 m01 = vec2_mid(p0, c1);
    Vec2 m12 = vec2_mid(c1, c2);
    Vec2 m23 = vec2_mid(c2, p3);
    Vec2 m012 = vec2_mid(m01, m12);
    Vec2 m123 = vec2_mid(m12, m23);
    Vec2 m = vec2_mid(m012, m123);
    glyph_emit_cubic(e, p0, m01, m012, m, depth - 1);
    glyph_emit_cubic(e, m, m123, m23, p3, depth - 1);
}

// Outline space (pixels, y-up, pen-relative) -> padded-cell-local texel coordinates (y-down).
static Vec2 glyph_cell_point(FlVec2 p, f32 origin_x, f32 origin_y) {
    return (Vec2) { p.x - origin_x, origin_y - p.y };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u32 gpu_glyph_lower_segments(const struct FlGlyphSegment* segments, u32 segment_count, f32 origin_x, f32 origin_y,
                             f32* out_floats) {
    GlyphQuadEmit emit = { .floats = out_floats };
    for (u32 i = 0; i < segment_count; i++) {
        const FlGlyphSegment* seg = &segments[i];
        switch (seg->kind) {
            case FlGlyphSegment_Line: {
                Vec2 a = glyph_cell_point(seg->pts[0], origin_x, origin_y);
                Vec2 b = glyph_cell_point(seg->pts[1], origin_x, origin_y);
                glyph_emit_quad(&emit, a, vec2_mid(a, b), b); // degenerate quadratic
                break;
            }
            case FlGlyphSegment_Quadratic:
                glyph_emit_quad(&emit, glyph_cell_point(seg->pts[0], origin_x, origin_y),
                                glyph_cell_point(seg->pts[1], origin_x, origin_y),
                                glyph_cell_point(seg->pts[2], origin_x, origin_y));
                break;
            case FlGlyphSegment_Cubic:
                glyph_emit_cubic(&emit, glyph_cell_point(seg->pts[0], origin_x, origin_y),
                                 glyph_cell_point(seg->pts[1], origin_x, origin_y),
                                 glyph_cell_point(seg->pts[2], origin_x, origin_y),
                                 glyph_cell_point(seg->pts[3], origin_x, origin_y), 2);
                break;
        }
    }
    return emit.quad_count;
}

u32 gpu_glyph_lower_outline(const struct FlGlyphOutline* outline, f32 origin_x, f32 origin_y, f32* out_floats) {
    return gpu_glyph_lower_segments(outline->segments, outline->segment_count, origin_x, origin_y, out_floats);
}
