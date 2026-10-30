#include "gpu_prepare_core.h"
#include "gpu_glyph_atlas.h"
#include "gpu_glyph_lower.h"
#include <core/arena.h>
#include <core/log.h>
#include <math.h>
#include <ui/font_cache.h>
#include <ui/image_decode.h>
#include <ui/image_system.h>
#include <ui/vector_image.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool gpu_prepare_image_decode_valid(FlImageLoadRequestId request_id, const FlImageDecodeResult* decoded,
                                    const FlImageLoadRequest* request) {
    if (!decoded) {
        log_error("gpu backend: decode was null for '%.*s' (request_id=%u)", (int)request->relative_path.length,
                  request->relative_path.data, (unsigned)request_id);
        return false;
    }
    if (decoded->width <= 0 || decoded->height <= 0 || (decoded->channels != 1 && decoded->channels != 4)) {
        log_error("gpu backend: unsupported decode shape %dx%d ch=%d (request_id=%u)", decoded->width, decoded->height,
                  decoded->channels, (unsigned)request_id);
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool gpu_prepare_image_wants_slot_tier(const FlImageDecodeResult* decoded, const FlImageLoadRequest* request) {
    return request->dedicated || decoded->width > GPU_IMAGE_ATLAS_MAX_DIM || decoded->height > GPU_IMAGE_ATLAS_MAX_DIM;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool gpu_prepare_image_place_atlas(GpuImageAtlas* atlas, FlImageLoadRequestId request_id,
                                   const FlImageDecodeResult* decoded, FlArena* scratch, GpuAtlasImageUpload* out) {
    u32 w = (u32)decoded->width;
    u32 h = (u32)decoded->height;
    const GpuImageAtlasEntry* entry = gpu_image_atlas_place(atlas, (FlImageHandle)request_id, w, h);
    if (entry == NULL) {
        return false; // gpu_image_atlas_place logged the reason
    }

    const u8* pixels = decoded->pixels;
    u32 stride = (u32)decoded->stride;
    if (decoded->channels == 1) {
        u8* rgba = arena_alloc_array(scratch, u8, (size_t)w * h * 4u);
        gpu_image_atlas_expand_coverage(rgba, decoded->pixels, w, h, stride);
        pixels = rgba;
        stride = w * 4u;
    }
    *out = (GpuAtlasImageUpload) {
        .x = entry->cell.x,
        .y = entry->cell.y,
        .w = w,
        .h = h,
        .pixels = pixels,
        .stride = stride,
    };
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
GpuGlyphCell gpu_prepare_glyph_cell(const FlGlyphOutline* outline) {
    f32 x0 = floorf(outline->min_x);
    f32 y1 = ceilf(outline->max_y);
    u32 w = (u32)(ceilf(outline->max_x) - x0);
    u32 h = (u32)(y1 - floorf(outline->min_y));

    if (outline->segment_count == 0 || w == 0 || h == 0) {
        return (GpuGlyphCell) { .empty = true };
    }
    return (GpuGlyphCell) {
        .pw = w + 2 * GPU_GLYPH_ATLAS_PAD,
        .ph = h + 2 * GPU_GLYPH_ATLAS_PAD,
        .left = x0 - (f32)GPU_GLYPH_ATLAS_PAD,
        .top = y1 + (f32)GPU_GLYPH_ATLAS_PAD,
        .empty = false,
    };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool gpu_prepare_glyph_place(GpuGlyphAtlas* atlas, FlArena* arena, FlFontId font, u16 size, u32 glyph_id,
                             const FlGlyphOutline* outline, GpuGlyphRaster* out) {
    if (outline == NULL) {
        return false;
    }
    if (gpu_glyph_atlas_find(atlas, font, size, glyph_id) != NULL) {
        return false;
    }

    GpuGlyphCell cell = gpu_prepare_glyph_cell(outline);
    const GpuGlyphEntry* entry
        = gpu_glyph_atlas_place(atlas, font, size, glyph_id, cell.pw, cell.ph, cell.left, cell.top, outline->advance);
    if (cell.empty) {
        return false;
    }
    if (entry == NULL) {
        // The repack is deferred to the end of render().
        gpu_glyph_atlas_on_overflow(atlas);
        return false;
    }

    f32* floats
        = arena_alloc_array(arena, f32, (size_t)outline->segment_count * GPU_GLYPH_LOWER_MAX_QUADS_PER_SEGMENT * 6);
    u32 quad_count = gpu_glyph_lower_outline(outline, entry->left, entry->top, floats);
    *out = (GpuGlyphRaster) { .entry = entry, .cell = cell, .floats = floats, .quad_count = quad_count };
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

bool gpu_prepare_svg_cell_geometry(FlArena* arena, const GpuSvgEntry* entry, const FlVectorImage* vector, bool color,
                                   GpuSvgCellGeometry* out) {
    if (vector == NULL || vector->shape_count == 0) {
        return false;
    }
    GpuSvgCellFit fit;
    if (!gpu_prepare_svg_fit(vector, entry->size, &fit)) {
        return false;
    }
    // Content height: the placed cell is content_h plus the padding ring. Taken off the entry (not the fit) so a
    // survivor re-rasters against the cell it actually occupies.
    f32 content_h = (f32)(entry->cell.h - 2 * GPU_SVG_ATLAS_PAD);

    u32 total_segments = 0;
    for (u32 s = 0; s < vector->shape_count; s++) {
        total_segments += vector->shapes[s].segment_count;
    }
    if (total_segments == 0) {
        return false;
    }

    f32* floats = arena_alloc_array(arena, f32, (size_t)total_segments * GPU_GLYPH_LOWER_MAX_QUADS_PER_SEGMENT * 6);
    *out = (GpuSvgCellGeometry) { .floats = floats };

    if (color) {
        // Lower every shape independently into the shared curve buffer, each recording its contiguous quad range
        // and baked premultiplied color - the color shader composites them in document order (painter's
        // algorithm).
        out->shapes = arena_alloc_array(arena, GpuColorShape, vector->shape_count);
        out->quad_count
            = gpu_svg_color_lower(arena, vector, fit.scale, content_h, floats, out->shapes, &out->shape_count);
        return true;
    }

    // Concatenate every shape's contours (a monochrome icon shares one fill across all of them), scaled into
    // raster pixel space, then lower the soup through the shared glyph path: holes wound opposite cancel under
    // signed-area accumulation, exactly as a glyph counter does.
    FlGlyphSegment* scaled = arena_alloc_array(arena, FlGlyphSegment, total_segments);
    u32 w = 0;
    for (u32 s = 0; s < vector->shape_count; s++) {
        const FlVectorShape* shape = &vector->shapes[s];
        for (u32 i = 0; i < shape->segment_count; i++) {
            FlGlyphSegment seg = shape->segments[i];
            for (u32 p = 0; p < 4; p++) {
                seg.pts[p].x *= fit.scale;
                seg.pts[p].y *= fit.scale;
            }
            scaled[w++] = seg;
        }
    }

    // Cell origin (-PAD, content_h + PAD) flips y-up document space into the y-down padded cell, matching
    // gpu_svg_color_lower's origin (gpu_glyph_lower_segments: cell x = p.x - origin_x, cell y = origin_y - p.y).
    out->quad_count = gpu_glyph_lower_segments(scaled, total_segments, -(f32)GPU_SVG_ATLAS_PAD,
                                               content_h + (f32)GPU_SVG_ATLAS_PAD, floats);
    return true;
}

void gpu_prepare_vector_into(void* backend, GpuSvgAtlas* atlas, FlImageHandle handle, u16 size,
                             const FlVectorImage* vector, bool color, GpuRasterCellFn raster_cell, u32* compact_count) {
    const GpuSvgEntry* live = gpu_svg_atlas_find(atlas, handle);
    if (live != NULL && live->size == size) {
        return; // already rasterized at this size - no re-dispatch
    }

    GpuSvgCellFit fit;
    if (!gpu_prepare_svg_fit(vector, size, &fit)) {
        return;
    }

    const GpuSvgEntry* entry
        = gpu_svg_atlas_place(atlas, handle, size, fit.pw, fit.ph, vector->classification, vector->mono_color, vector);
    if (entry == NULL) {
        // Atlas full: compact wholesale - reclaim the orphaned cells by repacking only the live ones - then
        // re-raster every survivor into its new position from its retained geometry (the texels still sit at the
        // pre-compaction positions) before retrying.
        gpu_svg_atlas_compact(atlas);
        (*compact_count)++;
        for (u32 i = 0; i < atlas->entry_count; i++) {
            const GpuSvgEntry* survivor = &atlas->entries[i];
            raster_cell(backend, survivor, survivor->vector, color);
        }
        entry = gpu_svg_atlas_place(atlas, handle, size, fit.pw, fit.ph, vector->classification, vector->mono_color,
                                    vector);
        if (entry == NULL) {
            return;
        }
    }

    raster_cell(backend, entry, vector, color);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void gpu_prepare_vector(void* backend, GpuSvgAtlas* mono_atlas, GpuSvgAtlas* color_atlas, FlImageHandle handle,
                        u16 size, const FlVectorImage* vector, GpuRasterCellFn raster_cell, u32* compact_count) {
    if (vector == NULL || vector->shape_count == 0) {
        return;
    }
    bool color = vector->classification != FlVectorClass_Monochrome;
    gpu_prepare_vector_into(backend, color ? color_atlas : mono_atlas, handle, size, vector, color, raster_cell,
                            compact_count);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void gpu_prepare_scale_changed(float* last_ui_scale, float ui_scale, FlArena* perm_arena, GpuGlyphAtlas* glyph_atlas,
                               GpuSvgAtlas* svg_atlas, GpuSvgAtlas* color_svg_atlas) {
    if (ui_scale == *last_ui_scale) {
        return;
    }
    *last_ui_scale = ui_scale;
    gpu_glyph_atlas_init(glyph_atlas, perm_arena);
    gpu_svg_atlas_init(svg_atlas, perm_arena, svg_atlas->format);
    gpu_svg_atlas_init(color_svg_atlas, perm_arena, color_svg_atlas->format);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void gpu_prepare_log_slot_overflow(const GpuImageSlots* slots) {
    if (slots->overflow > 0) {
        log_error("gpu backend: image sampler slots exhausted (%d) - %u image command(s) dropped this frame",
                  GPU_IMAGE_SLOT_COUNT, (unsigned)slots->overflow);
    }
}
