// Metal resource-prepare seams: image, glyph, and SVG vector realization.

#import <Foundation/Foundation.h>

#include "gpu_metal_internal.h"
#include "gpu_bindings.h"        // GPU_BIND_*
#include "gpu_debug.h"           // GPU_TILE_SIZE
#include "gpu_prepare_core.h"    // backend-agnostic prepare front halves
#include "gpu_svg_color_lower.h" // GpuColorShape
#include <core/log.h>
#include <ui/font_cache.h>     // FlGlyphOutline
#include <ui/image_decode.h>   // FlImageDecodeResult
#include <ui/image_registry.h> // image_registry_mark_realized
#include <ui/vector_image.h>   // FlVectorImage

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The cell-raster dispatch shared by glyph, monochrome SVG, and multi-color SVG cells; they differ in pipeline,
// target atlas, and whether per-shape records ride along. Each dispatch rides its own command buffer waited to
// completion. The curve and shape buffers are +1 owned and released once it completes.

typedef struct MetalCellDispatch {
    id<MTLComputePipelineState> pipeline; // glyph_pipeline (R8 coverage) or color_svg_pipeline (baked RGBA8)
    id<MTLTexture> target;                // the atlas cell the dispatch image-stores into
    const f32* curves;                    // the lowered quadratics: 3 float2 per quad, padded-cell-local
    u32 quad_count;
    // The color program's per-shape records (quad range, premultiplied color, fill rule), NULL for the coverage
    // program. NULL also selects the coverage binding set below.
    const GpuColorShape* shapes;
    u32 shape_count;
    u32 origin[2]; // the padded cell's texel position in the atlas
    u32 size[2];   // the padded cell's extent; every texel is written
} MetalCellDispatch;

// Returns false when an upload buffer could not be allocated (logged at its site); the dispatch is then skipped
// and the placed cell stays unrastered. The caller rewinds the frame arena either way.
static bool metal_dispatch_cell(MetalShared* self, const MetalCellDispatch* d) {
    @autoreleasepool {
        // The curve buffer: quad_count * (3 float2) verbatim.
        id<MTLBuffer> curve_buf = metal_upload_ssbo(self->device, d->curves,
                                                    (NSUInteger)d->quad_count * 6 * sizeof(f32), // +1 owned
                                                    6 * sizeof(f32));
        if (curve_buf == nil) {
            return false;
        }
        id<MTLBuffer> shape_buf = nil;
        if (d->shapes != NULL) {
            // Taken before the encoder opens so a failure leaves nothing half-encoded.
            shape_buf = metal_upload_ssbo(self->device, d->shapes, (NSUInteger)d->shape_count * sizeof(GpuColorShape),
                                          sizeof(GpuColorShape)); // +1 owned
            if (shape_buf == nil) {
                METAL_RELEASE(curve_buf);
                return false;
            }
        }

        id<MTLCommandBuffer> cmd = [self->queue commandBuffer];
        id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
        [enc setComputePipelineState:d->pipeline];
        if (shape_buf != nil) {
            u32 shape_count = d->shape_count;
            [enc setBuffer:curve_buf offset:0 atIndex:GPU_BIND_COLOR_SVG_CURVES];
            [enc setBuffer:shape_buf offset:0 atIndex:GPU_BIND_COLOR_SVG_SHAPES];
            [enc setBytes:d->origin length:sizeof d->origin atIndex:GPU_BIND_COLOR_SVG_CELL_ORIGIN];
            [enc setBytes:d->size length:sizeof d->size atIndex:GPU_BIND_COLOR_SVG_CELL_SIZE];
            [enc setBytes:&shape_count length:sizeof shape_count atIndex:GPU_BIND_COLOR_SVG_SHAPE_COUNT];
            [enc setTexture:d->target atIndex:GPU_BIND_COLOR_SVG_TEX_ATLAS];
        } else {
            u32 seg_count = d->quad_count;
            [enc setBuffer:curve_buf offset:0 atIndex:GPU_BIND_GLYPH_CURVES];
            [enc setBytes:d->origin length:sizeof d->origin atIndex:GPU_BIND_GLYPH_CELL_ORIGIN];
            [enc setBytes:d->size length:sizeof d->size atIndex:GPU_BIND_GLYPH_CELL_SIZE];
            [enc setBytes:&seg_count length:sizeof seg_count atIndex:GPU_BIND_GLYPH_SEG_COUNT];
            [enc setTexture:d->target atIndex:GPU_BIND_GLYPH_TEX_ATLAS];
        }
        // One thread per cell texel: GPU_TILE_SIZE-square threadgroups over ceil(cell / GPU_TILE_SIZE) groups.
        MTLSize threads = MTLSizeMake(GPU_TILE_SIZE, GPU_TILE_SIZE, 1);
        MTLSize groups = MTLSizeMake((d->size[0] + GPU_TILE_SIZE - 1) / GPU_TILE_SIZE,
                                     (d->size[1] + GPU_TILE_SIZE - 1) / GPU_TILE_SIZE, 1);
        [enc dispatchThreadgroups:groups threadsPerThreadgroup:threads];
        [enc endEncoding];
        [cmd commit];
        [cmd waitUntilCompleted];
        METAL_RELEASE(curve_buf);
        METAL_RELEASE(shape_buf);
    }
    return true;
}

// Image preparation seam: place a decoded static image into the RGBA8 atlas and upload its cell, or realize it on
// its own slot texture (gpu_prepare_image_wants_slot_tier), then ack the shared registry. Runs on the render
// thread.
void metal_prepare_image(void* user_data, FlImageLoadRequestId request_id, const FlImageDecodeResult* decoded,
                         FlArena* scratch, const FlImageLoadRequest* request) {
    MetalShared* self = user_data;

    if (!gpu_prepare_image_decode_valid(request_id, decoded, request)) {
        return;
    }

    u32 w = (u32)decoded->width;
    u32 h = (u32)decoded->height;
    if (gpu_prepare_image_wants_slot_tier(decoded, request)) {
        if (w > METAL_SLOT_MAX_TEXTURE_DIM || h > METAL_SLOT_MAX_TEXTURE_DIM) {
            log_error("metal backend: %ux%u image exceeds the texture limit (%d) - dropped", w, h,
                      METAL_SLOT_MAX_TEXTURE_DIM);
            return;
        }
        bool alpha_only = decoded->channels == 1;
        u32 tex = metal_slot_realize(self, w, h, alpha_only, false); // +1 owned by the pool; 0 = failed (logged)
        if (tex == 0) {
            return;
        }
        // An alpha-coverage source stays R8; the texture's swizzle (metal_slot_realize) makes it white x coverage.
        metal_texture_upload(self->slot_tex_pool[tex - 1], 0, 0, w, h, (u32)decoded->stride, alpha_only ? 1u : 4u,
                             decoded->pixels);
        if (gpu_image_slots_register_static(&self->cpu.image_slots, (FlImageHandle)request_id, w, h, tex) < 0) {
            // Table full or the handle is already registered - gpu_image_slots_register_static logged the reason.
            METAL_RELEASE(self->slot_tex_pool[tex - 1]);
            return;
        }
        image_registry_mark_realized((ImageHandle)request_id, decoded->width, decoded->height);
        return;
    }

    GpuAtlasImageUpload upload;
    if (!gpu_prepare_image_place_atlas(&self->cpu.image_atlas, request_id, decoded, scratch, &upload)) {
        return;
    }
    metal_texture_upload(self->image_atlas_tex, upload.x, upload.y, upload.w, upload.h, upload.stride, 4,
                         upload.pixels);
    image_registry_mark_realized((ImageHandle)request_id, decoded->width, decoded->height);
}

// Glyph preparation seam: place the glyph and rasterize its lowered outline into the R8 glyph atlas with one
// compute dispatch on its own command buffer. Runs on the render thread.
void metal_prepare_glyph(void* user_data, FlFontId font, uint16_t size, uint32_t glyph_id,
                         const FlGlyphOutline* outline) {
    MetalShared* self = user_data;
    GpuGlyphRaster glyph;
    if (!gpu_prepare_glyph_place(&self->cpu.glyph_atlas, self->frame_arena, font, size, glyph_id, outline, &glyph)) {
        return;
    }

    metal_dispatch_cell(self, &(MetalCellDispatch) {
                                  .pipeline = self->glyph_pipeline,
                                  .target = self->glyph_atlas_tex,
                                  .curves = glyph.floats,
                                  .quad_count = glyph.quad_count,
                                  .origin = { glyph.entry->cell.x, glyph.entry->cell.y },
                                  .size = { glyph.cell.pw, glyph.cell.ph },
                              });

    fl_arena_rewind(self->frame_arena);
}

// Rasterize one live SVG cell into its atlas cell: the GpuRasterCellFn the shared vector-prepare calls for a
// cell's first raster and for each survivor's re-raster after a compaction. A monochrome cell (color false)
// rasterizes R8 coverage through glyph_pipeline into svg_atlas_tex; a multi-color cell composites per-shape records
// through color_svg_pipeline into color_svg_atlas_tex. The frame arena backs the lowered geometry and is rewound
// here. Ticks svg_raster_count.
static void metal_raster_svg_cell(void* backend, const GpuSvgEntry* entry, const FlVectorImage* vector, bool color) {
    MetalShared* self = backend;
    GpuSvgCellGeometry geom;
    if (!gpu_prepare_svg_cell_geometry(self->frame_arena, entry, vector, color, &geom)) {
        return;
    }

    bool rastered = metal_dispatch_cell(self, &(MetalCellDispatch) {
                                                  .pipeline = color ? self->color_svg_pipeline : self->glyph_pipeline,
                                                  .target = color ? self->color_svg_atlas_tex : self->svg_atlas_tex,
                                                  .curves = geom.floats,
                                                  .quad_count = geom.quad_count,
                                                  .shapes = geom.shapes,
                                                  .shape_count = geom.shape_count,
                                                  .origin = { entry->cell.x, entry->cell.y },
                                                  .size = { entry->cell.w, entry->cell.h },
                                              });
    if (rastered) {
        self->cpu.svg_raster_count++; // a cell was actually rasterized (a step crossing, or a compaction re-raster)
    }
    fl_arena_rewind(self->frame_arena);
}

// Vector preparation seam: rasterize a solid-fill icon into the mono or multi-color SVG atlas through
// gpu_prepare_vector. Runs on the render thread.
void metal_prepare_vector(void* user_data, FlImageHandle handle, uint16_t size, const FlVectorImage* vector) {
    MetalShared* self = user_data;
    gpu_prepare_vector(self, &self->cpu.svg_atlas, &self->cpu.color_svg_atlas, handle, size, vector,
                       metal_raster_svg_cell, &self->cpu.svg_compact_count);
}
