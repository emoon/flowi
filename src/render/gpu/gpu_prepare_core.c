#include "gpu_prepare_core.h"
#include "gpu_glyph_atlas.h"
#include "gpu_glyph_lower.h"
#include <core/arena.h>
#include <core/log.h>
#include <math.h>
#include <ui/font_cache.h>
#include <ui/image/image_decode.h>
#include <ui/image/image_system.h>

static bool decode_shape_supported(const FlImageDecodeResult* decoded) {
    if (decoded->width <= 0 || decoded->height <= 0) {
        return false;
    }
    return decoded->channels == 1 || decoded->channels == 4;
}

bool gpu_prepare_image_decode_valid(FlImageLoadRequestId request_id, const FlImageDecodeResult* decoded,
                                    const FlImageLoadRequest* request) {
    if (!decoded) {
        log_error("gpu backend: decode was null for '%.*s' (request_id=%u)", (int)request->relative_path.length,
                  request->relative_path.data, (unsigned)request_id);
        return false;
    }
    if (!decode_shape_supported(decoded)) {
        log_error("gpu backend: unsupported decode shape %dx%d ch=%d (request_id=%u)", decoded->width, decoded->height,
                  decoded->channels, (unsigned)request_id);
        return false;
    }
    return true;
}

bool gpu_prepare_image_wants_slot_tier(const FlImageDecodeResult* decoded, const FlImageLoadRequest* request) {
    return request->dedicated || decoded->width > GPU_IMAGE_ATLAS_MAX_DIM || decoded->height > GPU_IMAGE_ATLAS_MAX_DIM;
}

GpuAtlasImageUpload gpu_prepare_image_place_atlas(GpuImageAtlas* atlas, FlImageLoadRequestId request_id,
                                                  const FlImageDecodeResult* decoded, FlArena* scratch) {
    u32 w = (u32)decoded->width;
    u32 h = (u32)decoded->height;
    const GpuImageAtlasEntry* entry = gpu_image_atlas_place(atlas, (FlImageHandle)request_id, w, h);
    if (entry == nullptr) {
        return (GpuAtlasImageUpload) { 0 }; // gpu_image_atlas_place logged the reason
    }

    const u8* pixels = decoded->pixels;
    u32 stride = (u32)decoded->stride;
    if (decoded->channels == 1) {
        u8* rgba = arena_alloc_array(scratch, u8, (size_t)w * h * 4u);
        gpu_image_atlas_expand_coverage(rgba, decoded->pixels, w, h, stride);
        pixels = rgba;
        stride = w * 4u;
    }
    return (GpuAtlasImageUpload) {
        .x = entry->cell.x,
        .y = entry->cell.y,
        .w = w,
        .h = h,
        .pixels = pixels,
        .stride = stride,
    };
}

GpuGlyphCell gpu_prepare_glyph_cell(const FlGlyphOutline* outline) {
    GpuGlyphCell empty = { .empty = true };
    if (outline->segment_count == 0) {
        return empty;
    }

    f32 x0 = floorf(outline->min_x);
    f32 y1 = ceilf(outline->max_y);
    u32 w = (u32)(ceilf(outline->max_x) - x0);
    u32 h = (u32)(y1 - floorf(outline->min_y));
    if (w == 0 || h == 0) {
        return empty;
    }
    return (GpuGlyphCell) {
        .pw = w + 2 * GPU_GLYPH_ATLAS_PAD,
        .ph = h + 2 * GPU_GLYPH_ATLAS_PAD,
        .left = x0 - (f32)GPU_GLYPH_ATLAS_PAD,
        .top = y1 + (f32)GPU_GLYPH_ATLAS_PAD,
        .empty = false,
    };
}

GpuGlyphRaster gpu_prepare_glyph_place(GpuGlyphAtlas* atlas, FlArena* arena, GpuGlyphKey key,
                                       const FlGlyphOutline* outline) {
    GpuGlyphRaster none = { 0 };
    if (outline == nullptr) {
        return none;
    }
    if (gpu_glyph_atlas_find(atlas, key.font, key.size, key.glyph_index) != nullptr) {
        return none;
    }

    GpuGlyphCell cell = gpu_prepare_glyph_cell(outline);
    const GpuGlyphEntry* entry = gpu_glyph_atlas_place(atlas, (GpuGlyphKey) { key.font, key.size, key.glyph_index }, (GpuGlyphMetrics) { cell.pw, cell.ph, cell.left, cell.top, outline->advance });
    if (cell.empty) {
        return none;
    }
    if (entry == nullptr) {
        // The repack is deferred to the end of render().
        gpu_glyph_atlas_on_overflow(atlas);
        return none;
    }

    f32* floats
        = arena_alloc_array(arena, f32, (size_t)outline->segment_count * GPU_GLYPH_LOWER_MAX_QUADS_PER_SEGMENT * 6);
    u32 quad_count = gpu_glyph_lower_outline(outline, entry->left, entry->top, floats);
    return (GpuGlyphRaster) { .entry = entry, .cell = cell, .floats = floats, .quad_count = quad_count };
}

void gpu_prepare_scale_changed(GpuCpuState* cpu, float* last_ui_scale, float ui_scale, FlArena* perm_arena) {
    if (ui_scale == *last_ui_scale) {
        return;
    }
    *last_ui_scale = ui_scale;
    gpu_glyph_atlas_init(&cpu->glyph_atlas, perm_arena);
    gpu_svg_atlas_init(&cpu->svg_atlas, perm_arena, cpu->svg_atlas.format);
    gpu_svg_atlas_init(&cpu->color_svg_atlas, perm_arena, cpu->color_svg_atlas.format);
}

void gpu_prepare_log_slot_overflow(const GpuImageSlots* slots) {
    if (slots->overflow > 0) {
        log_error("gpu backend: image sampler slots exhausted (%d) - %u image command(s) dropped this frame",
                  GPU_IMAGE_SLOT_COUNT, (unsigned)slots->overflow);
    }
}
