#include "scene.h"
#include <render/gpu/gpu_svg_atlas.h>

void scene_fill_rect(const Painter* p, FlRect r, FlCornerRadius cr, const FlColor corners[4]) {
    fl_ir_fill_rect(p->arena, v_rect(p->view, r), v_radius(p->view, cr), corners, nullptr);
}

void scene_fill_solid(const Painter* p, FlRect r, FlCornerRadius cr, FlColor color) {
    scene_fill_rect(p, r, cr, (FlColor[4]) { color, color, color, color });
}

void scene_border(const Painter* p, FlRect r, BorderSpec border) {
    View v = p->view;
    fl_ir_border(p->arena, v_rect(v, r), v_len(v, border.left), v_len(v, border.right), v_len(v, border.top),
                 v_len(v, border.bottom), v_radius(v, border.radius), border.color);
}

void scene_circle(const Painter* p, FlVec2 center, f32 radius, ShapeStyle style) {
    View v = p->view;
    fl_ir_circle(p->arena, v_x(v, center.x), v_y(v, center.y), v_len(v, radius), style.fill, style.stroke,
                 v_len(v, style.stroke_width), style.effect);
}

void scene_diamond(const Painter* p, FlVec2 center, FlVec2 radii, ShapeStyle style) {
    View v = p->view;
    fl_ir_diamond(p->arena, v_x(v, center.x), v_y(v, center.y), v_len(v, radii.x), v_len(v, radii.y), style.fill,
                  style.stroke, v_len(v, style.stroke_width), style.effect);
}

void scene_arc(const Painter* p, ArcSpec arc) {
    View v = p->view;
    fl_ir_arc(p->arena, v_x(v, arc.center.x), v_y(v, arc.center.y), v_len(v, arc.radius), v_len(v, arc.thickness),
              arc.start, arc.sweep, arc.color, arc.flags, nullptr);
}

FlPolyPoint* scene_polyline(const Painter* p, PolylineSpec spec) {
    View v = p->view;
    return fl_ir_polyline(p->arena, spec.color, spec.fill_color, v_len(v, spec.width), v_y(v, spec.baseline_y),
                          spec.flags, spec.point_count, nullptr);
}

FlBar* scene_bars(const Painter* p, f32 baseline_y, FlColor color, u32 count) {
    return fl_ir_bars(p->arena, v_y(p->view, baseline_y), color, count, nullptr);
}

void scene_image(const Painter* p, FlImageSource src, FlRect dst, ImageStyle style) {
    fl_ir_image(p->arena, src, FlImageMode_Color, v_rect(p->view, dst), (FlRect) { 0 }, style.tint, style.filter,
                nullptr);
}

// Draws an SVG icon that stays sharp under zoom, as scene_text does for glyphs. The backend rasterizes the
// contours into its SVG atlas keyed by (handle, quantized size), so this prepares every frame at the size the
// zoomed dst needs: svg_raster_size_for_dst quantizes, and prepare_vector returns early unless the size crosses
// a quantized step. Monochrome icons resolve to tintable coverage; multi-color ones to baked RGBA that ignores
// the tint. Does nothing when the image is nullptr (parse failure).
void scene_svg(const Painter* p, SvgIcon icon, FlRect dst_world, FlColor tint) {
    if (icon.image == nullptr) {
        return;
    }
    FlRenderBackend* backend = p->backend;
    if (backend->prepare_vector != nullptr) {
        u16 size = svg_raster_size_for_dst(v_len(p->view, dst_world.w), v_len(p->view, dst_world.h),
                                           icon.image->classification);
        backend->prepare_vector(backend->user_data, icon.handle, size, icon.image);
    }
    scene_image(p, (FlImageSource) { .kind = FlImageKind_Static, .handle = icon.handle }, dst_world,
                (ImageStyle) { tint, FlImageFilter_Linear });
}

typedef struct TextSizes {
    u16 raster_q4;
    f32 scale;
    u16 metrics;
} TextSizes;

// size_world is the font size at zoom 1. A glyph's pixel size must be integral, so text follows the zoom in
// two parts:
//
//  1. Glyphs rasterize, and the atlas keys, at raster: the zoomed size snapped to font_cache's quantization
//     grid. The run carries scale = display / raster and the backend scales each glyph quad by it.
//
//  2. The run is laid out in world space at a fixed metrics size and the view scales the pen positions by
//     zoom, like every other primitive. Advancing the pen at the raster size would make the spacing step with
//     it, because hinting makes the advance per pixel vary across sizes and the error accumulates along the run.
static TextSizes text_sizes(f32 size_world, f32 zoom) {
    f32 display = size_world * zoom;
    if (display < 1.0f) {
        display = 1.0f;
    }
    u16 raster = font_cache_quantize_size((u16)(display + 0.5f));
    if (raster < 1) {
        raster = 1;
    }
    u16 metrics = (u16)(size_world + 0.5f);
    if (metrics < 1) {
        metrics = 1;
    }
    // The quarter-px key for the outline, atlas and run. raster is already bucket-quantized, so the key is
    // formed directly; font_cache_whole_px_to_q4 would re-bucket the band boundaries.
    return (TextSizes) { .raster_q4 = font_cache_quantize_size_q4((f32)raster),
                         .scale = display / (f32)raster,
                         .metrics = metrics };
}

static bool face_span_ends_at(const FlShapedRun* shaped, u32 i) {
    return i + 1 == shaped->count || shaped->glyphs[i + 1].font != shaped->glyphs[i].font;
}

static FlRect glyph_pen(FlVec2 origin, const FlShapedGlyph* sg) {
    return (FlRect) { origin.x + (f32)sg->x / 64.0f, origin.y - (f32)sg->y / 64.0f, 0.0f, 0.0f };
}

// Emits one run per face; render() prepares the glyphs through the bound font cache.
//
// Shape at the metrics size, in world space, and place each glyph at its view-mapped screen
// position. This is fl_ir_text_layout's loop with the advance size separate from the run's raster size, which
// that builder cannot express. Off-screen glyphs are dropped, so the run only references atlas cells that
// exist.
void scene_text(const Painter* p, TextSpec text) {
    const TextSizes sizes = text_sizes(text.size, p->view.zoom);

    // Cull per glyph: keep a glyph when its pen point is within one em of the visible world region. For the
    // Latin faces used here a glyph's ink stays within about one em of its pen. Without the cull the whole
    // gallery's text rasterizes at the zoomed pixel size and overflows the atlas packer, which drops glyphs.
    const f32 margin = text.size;

    const FlShapedRun shaped
        = font_cache_shape_run(p->font_cache, p->font, font_cache_whole_px_to_q4(sizes.metrics), text.text);
    fl_arena_rewind(p->scratch);
    FlGlyph* glyphs = arena_alloc_array(p->scratch, FlGlyph, shaped.count);
    u32 count = 0;
    for_count(i, shaped.count) {
        const FlShapedGlyph* sg = &shaped.glyphs[i];
        FlRect pen = glyph_pen(text.origin, sg);
        if (view_visible(p->view, pen, margin)) {
            glyphs[count++]
                = (FlGlyph) { .x = v_x(p->view, pen.x), .y = v_y(p->view, pen.y), .glyph_index = sg->glyph_index };
        }
        if (count > 0 && face_span_ends_at(&shaped, i)) {
            fl_ir_text_run(p->arena, sg->font, sizes.raster_q4, sizes.scale, text.color, glyphs, count);
            count = 0;
        }
    }
}
