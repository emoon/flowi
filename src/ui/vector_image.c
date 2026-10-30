#include "vector_image.h"

#include <math.h>

#include <core/arena.h>
#include <core/core.h>
#include <core/log.h>
#include <core/memory.h>
#include "nanosvg.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// nanosvg stores fill paint as 0xAABBGGRR; unpack into the neutral RGBA FlColor.

static FlColor svg_color(unsigned int c) {
    return (FlColor) { (u8)(c & 0xff), (u8)((c >> 8) & 0xff), (u8)((c >> 16) & 0xff), (u8)((c >> 24) & 0xff) };
}

static u32 svg_color_key(FlColor c) {
    return (u32)c.r | ((u32)c.g << 8) | ((u32)c.b << 16) | ((u32)c.a << 24);
}

// Document space is y-down (SVG); the shared lowering expects the glyph convention (y-up), so flip here.
static FlVec2 svg_point(const float* pts, int k, f32 height) {
    return (FlVec2) { pts[2 * k], height - pts[2 * k + 1] };
}

static bool svg_shape_visible(const NSVGshape* shape) {
    return (shape->flags & NSVG_FLAGS_VISIBLE) != 0;
}

static bool svg_shape_has_stroke(const NSVGshape* shape) {
    return shape->stroke.type != NSVG_PAINT_NONE && shape->strokeWidth > 0.0f;
}

// A visible shape is supported only if it has a solid color fill and no stroke. A visible shape with no fill
// and no stroke draws nothing and is simply skipped (not rejected).
typedef enum SvgShapeVerdict {
    SvgShape_Skip,        // contributes nothing - ignore
    SvgShape_SolidFill,   // supported: solid color fill
    SvgShape_Unsupported, // gradient or stroke - reject the image
} SvgShapeVerdict;

static SvgShapeVerdict svg_classify_shape(const NSVGshape* shape) {
    if (!svg_shape_visible(shape)) {
        return SvgShape_Skip;
    }
    if (svg_shape_has_stroke(shape)) {
        return SvgShape_Unsupported;
    }
    if (shape->fill.type == NSVG_PAINT_LINEAR_GRADIENT || shape->fill.type == NSVG_PAINT_RADIAL_GRADIENT) {
        return SvgShape_Unsupported;
    }
    if (shape->fill.type == NSVG_PAINT_COLOR) {
        return SvgShape_SolidFill;
    }
    return SvgShape_Skip; // NSVG_PAINT_NONE / NSVG_PAINT_UNDEF - no fill
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Count the closed-contour segments a shape lowers to: one Cubic per bezier piece plus, per path, a closing
// Line back to the path's first point when the path does not already return there.

static bool svg_path_needs_close(const NSVGpath* path, f32 height) {
    if (path->npts < 4) {
        return false;
    }
    FlVec2 first = svg_point(path->pts, 0, height);
    FlVec2 last = svg_point(path->pts, path->npts - 1, height);
    return fabsf(first.x - last.x) > 1e-4f || fabsf(first.y - last.y) > 1e-4f;
}

static u32 svg_shape_segment_count(const NSVGshape* shape, f32 height) {
    u32 count = 0;
    for (const NSVGpath* path = shape->paths; path; path = path->next) {
        if (path->npts < 4) {
            continue;
        }
        count += (u32)((path->npts - 1) / 3); // cubic pieces
        if (svg_path_needs_close(path, height)) {
            count += 1; // closing line
        }
    }
    return count;
}

static void svg_fill_shape_segments(const NSVGshape* shape, f32 height, FlGlyphSegment* out) {
    u32 i = 0;
    for (const NSVGpath* path = shape->paths; path; path = path->next) {
        if (path->npts < 4) {
            continue;
        }
        int pieces = (path->npts - 1) / 3;
        for (int p = 0; p < pieces; p++) {
            int base = p * 3;
            out[i].kind = FlGlyphSegment_Cubic;
            out[i].pts[0] = svg_point(path->pts, base + 0, height);
            out[i].pts[1] = svg_point(path->pts, base + 1, height);
            out[i].pts[2] = svg_point(path->pts, base + 2, height);
            out[i].pts[3] = svg_point(path->pts, base + 3, height);
            i++;
        }
        if (svg_path_needs_close(path, height)) {
            out[i].kind = FlGlyphSegment_Line;
            out[i].pts[0] = svg_point(path->pts, path->npts - 1, height);
            out[i].pts[1] = svg_point(path->pts, 0, height);
            i++;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool vector_image_parse(const u8* data, size_t size, struct FlArena* arena, FlVectorImage* out) {
    arena_scratch_auto_conflict(temp, arena);

    // nsvgParse mutates and requires a null-terminated buffer.
    char* svg_string = arena_alloc_array(temp.arena, char, size + 1);
    memory_copy(svg_string, size, data, size);
    svg_string[size] = '\0';

    NSVGimage* image = nsvgParse(svg_string, "px", 96.0f);
    if (!image) {
        log_warning("vector_image: SVG failed to parse (%zu bytes) — not loaded on the GPU vector path", size);
        return false;
    }

    f32 height = image->height;

    // First pass: validate and classify. Any visible gradient/stroke shape rejects the whole image.
    u32 supported = 0;
    bool have_color = false;
    u32 first_color_key = 0;
    bool multi_color = false;
    FlColor mono_color = { 0 };

    for (const NSVGshape* shape = image->shapes; shape; shape = shape->next) {
        SvgShapeVerdict verdict = svg_classify_shape(shape);
        if (verdict == SvgShape_Unsupported) {
            log_warning("vector_image: unsupported SVG — gradients/strokes are not supported on the GPU vector path; "
                        "flatten strokes to fills / remove gradients (Illustrator \"Outline Stroke\", Inkscape "
                        "\"Stroke to Path\", or svgo)");
            nsvgDelete(image);
            return false;
        }
        if (verdict != SvgShape_SolidFill) {
            continue;
        }
        if (svg_shape_segment_count(shape, height) == 0) {
            continue; // a solid fill with no drawable contour contributes nothing
        }
        FlColor fill = svg_color(shape->fill.color);
        u32 key = svg_color_key(fill);
        if (!have_color) {
            have_color = true;
            first_color_key = key;
            mono_color = fill;
        } else if (key != first_color_key) {
            multi_color = true;
        }
        supported++;
    }

    if (supported == 0) {
        log_warning("vector_image: SVG has no visible solid fill — nothing to draw on the GPU vector path");
        nsvgDelete(image);
        return false;
    }

    // Second pass: materialize the neutral contours into the persistent arena.
    FlVectorShape* shapes = arena_alloc_array(arena, FlVectorShape, supported);
    u32 out_index = 0;
    for (const NSVGshape* shape = image->shapes; shape; shape = shape->next) {
        if (svg_classify_shape(shape) != SvgShape_SolidFill) {
            continue;
        }
        u32 seg_count = svg_shape_segment_count(shape, height);
        if (seg_count == 0) {
            continue;
        }
        FlGlyphSegment* segments = arena_alloc_array(arena, FlGlyphSegment, seg_count);
        svg_fill_shape_segments(shape, height, segments);

        shapes[out_index].segments = segments;
        shapes[out_index].segment_count = seg_count;
        shapes[out_index].fill = svg_color(shape->fill.color);
        shapes[out_index].fill_rule = (shape->fillRule == NSVG_FILLRULE_EVENODD) ? 1 : 0;
        shapes[out_index].opacity = shape->opacity;
        out_index++;
    }

    out->shapes = shapes;
    out->shape_count = supported;
    out->classification = multi_color ? FlVectorClass_MultiColor : FlVectorClass_Monochrome;
    out->mono_color = mono_color;
    out->width = image->width;
    out->height = image->height;

    nsvgDelete(image);
    return true;
}
