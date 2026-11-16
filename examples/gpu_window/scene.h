#pragma once

#include "view.h"
#include <ui/font_cache.h>
#include <ui/render_backend.h>
#include <ui/render_ir_build.h>
#include <ui/vector_image.h>

typedef struct Painter {
    FlArena* arena;
    FlArena* scratch; // must be a different arena from `arena`
    FlRenderBackend* backend;
    FlFontCache* font_cache;
    FlFontId font;
    View view;
} Painter;

// A parsed SVG icon bound to the backend handle its prepared atlas cell is keyed under.
typedef struct SvgIcon {
    const FlVectorImage* image; // resolution-independent contours (nullptr if parse failed)
    FlImageHandle handle;
} SvgIcon;

typedef struct BorderSpec {
    f32 left;
    f32 right;
    f32 top;
    f32 bottom;
    FlCornerRadius radius;
    FlColor color;
} BorderSpec;

typedef struct ShapeStyle {
    FlColor fill;
    FlColor stroke;
    f32 stroke_width;
    const FlEffect* effect;
} ShapeStyle;

typedef struct ArcSpec {
    FlVec2 center;
    f32 radius;
    f32 thickness;
    f32 start;
    f32 sweep;
    FlColor color;
    u32 flags;
} ArcSpec;

typedef struct PolylineSpec {
    FlColor color;
    FlColor fill_color;
    f32 width;
    f32 baseline_y;
    u32 flags;
    u32 point_count;
} PolylineSpec;

typedef struct ImageStyle {
    FlColor tint;
    FlImageFilter filter;
} ImageStyle;

typedef struct TextSpec {
    f32 size;
    FlColor color;
    FlString text;
    FlVec2 origin; // pen x and baseline y
} TextSpec;

static inline FlCornerRadius radius_all(f32 r) {
    return (FlCornerRadius) { r, r, r, r };
}

void scene_fill_rect(const Painter* p, FlRect r, FlCornerRadius cr, const FlColor corners[4]);
void scene_fill_solid(const Painter* p, FlRect r, FlCornerRadius cr, FlColor color);
void scene_border(const Painter* p, FlRect r, BorderSpec border);
void scene_circle(const Painter* p, FlVec2 center, f32 radius, ShapeStyle style);
void scene_diamond(const Painter* p, FlVec2 center, FlVec2 radii, ShapeStyle style);
void scene_arc(const Painter* p, ArcSpec arc);
FlPolyPoint* scene_polyline(const Painter* p, PolylineSpec spec);
FlBar* scene_bars(const Painter* p, f32 baseline_y, FlColor color, u32 count);
void scene_image(const Painter* p, FlImageSource src, FlRect dst, ImageStyle style);
void scene_svg(const Painter* p, SvgIcon icon, FlRect dst_world, FlColor tint);
void scene_text(const Painter* p, TextSpec text);
