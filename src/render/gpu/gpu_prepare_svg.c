#include "gpu_prepare_svg.h"
#include "gpu_cpu_state.h"
#include "gpu_glyph_lower.h"
#include <core/arena.h>
#include <math.h>
#include <ui/font_cache.h>
#include <ui/vector_image.h>

typedef struct SvgLowering {
    const FlVectorImage* vector;
    f32 scale;
    f32 content_h;
    u32 segment_count;
} SvgLowering;

static bool has_shapes(const FlVectorImage* vector) {
    return vector != nullptr && vector->shape_count != 0;
}

bool gpu_prepare_svg_fit(const FlVectorImage* vector, u16 size, GpuSvgCellFit* out) {
    f32 doc_max = fmaxf(vector->width, vector->height);
    if (doc_max <= 0.0f) {
        return false;
    }
    // The contours are y-up document pixels, so the cell is content_w x content_h plus the padding ring.
    f32 scale = (f32)size / doc_max;
    u32 cw = (u32)ceilf(vector->width * scale);
    u32 ch = (u32)ceilf(vector->height * scale);
    if (cw == 0 || ch == 0) {
        return false;
    }
    out->scale = scale;
    out->pw = cw + 2 * GPU_SVG_ATLAS_PAD;
    out->ph = ch + 2 * GPU_SVG_ATLAS_PAD;
    return true;
}

static u32 total_segments(const FlVectorImage* vector) {
    u32 total = 0;
    for_count(s, vector->shape_count) {
        total += vector->shapes[s].segment_count;
    }
    return total;
}

static f32* alloc_quad_floats(FlArena* arena, u32 segment_count) {
    return arena_alloc_array(arena, f32, (size_t)segment_count * GPU_GLYPH_LOWER_MAX_QUADS_PER_SEGMENT * 6);
}

// Lower every shape independently into the shared curve buffer, each recording its contiguous quad range and baked
// premultiplied color. The color shader composites them in document order (painter's algorithm).
static GpuSvgCellGeometry lower_color(FlArena* arena, const SvgLowering* in) {
    GpuSvgCellGeometry geom = { .floats = alloc_quad_floats(arena, in->segment_count) };
    geom.shapes = arena_alloc_array(arena, GpuColorShape, in->vector->shape_count);
    geom.quad_count
        = gpu_svg_color_lower(arena, in->vector, in->scale, in->content_h, geom.floats, geom.shapes, &geom.shape_count);
    return geom;
}

static FlGlyphSegment scale_segment(FlGlyphSegment seg, f32 scale) {
    for_count(p, 4) {
        seg.pts[p].x *= scale;
        seg.pts[p].y *= scale;
    }
    return seg;
}

static FlGlyphSegment* scaled_contours(FlArena* arena, const SvgLowering* in) {
    FlGlyphSegment* scaled = arena_alloc_array(arena, FlGlyphSegment, in->segment_count);
    FlGlyphSegment* dst = scaled;
    for_count(s, in->vector->shape_count) {
        const FlVectorShape* shape = &in->vector->shapes[s];
        for_count(i, shape->segment_count) {
            *dst++ = scale_segment(shape->segments[i], in->scale);
        }
    }
    return scaled;
}

// Concatenate every shape's contours (a monochrome icon shares one fill across all of them), scaled into raster
// pixel space, then lower the soup through the shared glyph path: holes wound opposite cancel under signed-area
// accumulation, exactly as a glyph counter does.
static GpuSvgCellGeometry lower_mono(FlArena* arena, const SvgLowering* in) {
    f32* floats = alloc_quad_floats(arena, in->segment_count);
    FlGlyphSegment* scaled = scaled_contours(arena, in);

    // Cell origin (-PAD, content_h + PAD) flips y-up document space into the y-down padded cell, matching
    // gpu_svg_color_lower's origin (gpu_glyph_lower_segments: cell x = p.x - origin_x, cell y = origin_y - p.y).
    u32 quad_count = gpu_glyph_lower_segments(scaled, in->segment_count, -(f32)GPU_SVG_ATLAS_PAD,
                                              in->content_h + (f32)GPU_SVG_ATLAS_PAD, floats);
    return (GpuSvgCellGeometry) { .floats = floats, .quad_count = quad_count };
}

GpuSvgCellGeometry gpu_prepare_svg_cell_geometry(FlArena* arena, const GpuSvgEntry* entry, const FlVectorImage* vector,
                                                 bool color) {
    GpuSvgCellGeometry nothing = { 0 };
    GpuSvgCellFit fit;
    if (!has_shapes(vector) || !gpu_prepare_svg_fit(vector, entry->size, &fit)) {
        return nothing;
    }
    SvgLowering in = {
        .vector = vector,
        .scale = fit.scale,
        // Content height: the placed cell is content_h plus the padding ring. Taken off the entry (not the fit) so a
        // survivor re-rasters against the cell it occupies.
        .content_h = (f32)(entry->cell.h - 2 * GPU_SVG_ATLAS_PAD),
        .segment_count = total_segments(vector),
    };
    if (in.segment_count == 0) {
        return nothing;
    }
    return color ? lower_color(arena, &in) : lower_mono(arena, &in);
}

static const GpuSvgEntry* place_cell(GpuSvgAtlas* atlas, GpuSvgKey key, const GpuSvgCellFit* fit,
                                     const FlVectorImage* vector) {
    return gpu_svg_atlas_place(atlas, &(GpuSvgPlaceRequest) { key.handle, key.size, fit->pw, fit->ph, vector->classification, vector->mono_color, vector });
}

static void reraster_survivors(const GpuSvgRaster* raster, const GpuSvgAtlas* atlas, bool color) {
    for_count(i, atlas->entry_count) {
        const GpuSvgEntry* survivor = &atlas->entries[i];
        raster->fn(raster->backend, survivor, survivor->vector, color);
    }
}

bool gpu_prepare_vector_into(const GpuSvgRaster* raster, GpuSvgAtlas* atlas, GpuSvgKey key,
                             const FlVectorImage* vector) {
    const GpuSvgEntry* live = gpu_svg_atlas_find(atlas, key.handle);
    if (live != nullptr && live->size == key.size) {
        return false; // already rasterized at this size
    }

    GpuSvgCellFit fit;
    if (!gpu_prepare_svg_fit(vector, key.size, &fit)) {
        return false;
    }

    bool color = atlas->format == GpuSvgTexel_Rgba8;
    bool compacted = false;
    const GpuSvgEntry* entry = place_cell(atlas, key, &fit, vector);
    if (entry == nullptr) {
        // Atlas full: compact wholesale (reclaim the orphaned cells by repacking only the live ones), then
        // re-raster every survivor into its new position from its retained geometry (the texels still sit at the
        // pre-compaction positions) before retrying.
        gpu_svg_atlas_compact(atlas);
        compacted = true;
        reraster_survivors(raster, atlas, color);
        entry = place_cell(atlas, key, &fit, vector);
    }

    if (entry != nullptr) {
        raster->fn(raster->backend, entry, vector, color);
    }
    return compacted;
}

void gpu_prepare_vector(void* backend, GpuRasterCellFn raster_cell, GpuSvgKey key, const FlVectorImage* vector) {
    if (!has_shapes(vector)) {
        return;
    }
    GpuCpuState* cpu = backend;
    GpuSvgAtlas* atlas = vector->classification != FlVectorClass_Monochrome ? &cpu->color_svg_atlas : &cpu->svg_atlas;
    GpuSvgRaster raster = { .backend = backend, .fn = raster_cell };
    if (gpu_prepare_vector_into(&raster, atlas, key, vector)) {
        cpu->svg_compact_count++;
    }
}
