// Metal pixel readback (render_backend.h) and the gpu_debug.h seams: the offscreen target and the atlases blit into
// a transient shared MTLBuffer and copy out tight, row 0 first.

#import <Foundation/Foundation.h>

#include "gpu_debug.h"
#include "gpu_glyph_atlas.h"
#include "gpu_svg_atlas.h"
#include "gpu_metal_internal.h"
#include <core/log.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The one GPU -> CPU body every readback here shares: blit tex's w x h region into a transient shared MTLBuffer,
// wait for it, then strip the buffer's row padding into the caller's tight dst (height * tight_bpr bytes, row 0
// first). The blit's destination bytes-per-row is 256-aligned. The staging buffer is +1 owned and released here.
// Returns false on an allocation failure.

static bool metal_blit_texture_tight(MetalShared* self, id<MTLTexture> tex, NSUInteger width, NSUInteger height,
                                     NSUInteger tight_bpr, u8* dst) {
    NSUInteger aligned_bpr = (tight_bpr + 255u) & ~(NSUInteger)255u;

    id<MTLBuffer> staging = [self->device newBufferWithLength:aligned_bpr * height
                                                      options:MTLResourceStorageModeShared]; // +1 owned
    if (staging == nil) {
        log_error("metal backend: %lu-byte readback staging buffer allocation failed",
                  (unsigned long)(aligned_bpr * height));
        return false;
    }

    id<MTLCommandBuffer> cmd = [self->queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
    [blit copyFromTexture:tex
                 sourceSlice:0
                 sourceLevel:0
                sourceOrigin:MTLOriginMake(0, 0, 0)
                  sourceSize:MTLSizeMake(width, height, 1)
                    toBuffer:staging
           destinationOffset:0
      destinationBytesPerRow:aligned_bpr
    destinationBytesPerImage:aligned_bpr * height];
    [blit endEncoding];
    [cmd commit];
    [cmd waitUntilCompleted];

    const u8* src = (const u8*)[staging contents];
    for (NSUInteger y = 0; y < height; y++) {
        memcpy(dst + y * tight_bpr, src + y * aligned_bpr, tight_bpr);
    }
    [staging release];
    return true;
}

bool metal_read_pixels(void* renderer_user_data, FlSurface* surface_handle, FlPixelReadback* out) {
    @autoreleasepool {
        MetalShared* self = renderer_user_data;
        MetalSurface* surface = (MetalSurface*)surface_handle;

        if (surface->target == nil) {
            return false; // the target allocation failed and was logged at its site; there is nothing to read
        }

        NSUInteger width = (NSUInteger)surface->width;
        NSUInteger height = (NSUInteger)surface->height;
        NSUInteger tight_bpr = width * 4u; // RGBA8: four interleaved bytes per texel

        if (!metal_blit_texture_tight(self, surface->target, width, height, tight_bpr, surface->readback_buf)) {
            return false;
        }

        *out = (FlPixelReadback) {
            .pixels = surface->readback_buf,
            .width = (u32)width,
            .height = (u32)height,
            .stride = (u32)tight_bpr,
        };
        return true;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Glyph-atlas debug seams (gpu_debug.h): the R8 coverage texels verbatim. The
// atlas is backend-wide, so this takes no surface.

bool gpu_debug_read_glyph_atlas(void* renderer_user_data, struct FlArena* arena, GpuGlyphAtlasReadback* out) {
    @autoreleasepool {
        MetalShared* self = renderer_user_data;

        NSUInteger dim = (NSUInteger)GPU_GLYPH_ATLAS_SIZE;
        NSUInteger tight_bpr = dim; // R8: one byte per texel

        u8* data = arena_alloc_array(arena, u8, (size_t)dim * (size_t)dim);
        if (!metal_blit_texture_tight(self, self->glyph_atlas_tex, dim, dim, tight_bpr, data)) {
            return false;
        }

        *out = (GpuGlyphAtlasReadback) {
            .texels = data,
            .width = (u32)dim,
            .height = (u32)dim,
        };
        return true;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SVG vector-icon atlas debug seams (gpu_debug.h): the glyph-atlas seam above for the R8 monochrome and RGBA8
// multi-color SVG atlases.

bool gpu_debug_read_svg_atlas(void* renderer_user_data, struct FlArena* arena, GpuSvgAtlasReadback* out) {
    @autoreleasepool {
        MetalShared* self = renderer_user_data;

        NSUInteger dim = (NSUInteger)GPU_SVG_ATLAS_SIZE_MONO;
        NSUInteger tight_bpr = dim; // R8: one byte per texel

        u8* data = arena_alloc_array(arena, u8, (size_t)dim * (size_t)dim);
        if (!metal_blit_texture_tight(self, self->svg_atlas_tex, dim, dim, tight_bpr, data)) {
            return false;
        }

        *out = (GpuSvgAtlasReadback) {
            .texels = data,
            .width = (u32)dim,
            .height = (u32)dim,
        };
        return true;
    }
}

bool gpu_debug_read_color_svg_atlas(void* renderer_user_data, struct FlArena* arena, GpuColorSvgAtlasReadback* out) {
    @autoreleasepool {
        MetalShared* self = renderer_user_data;

        NSUInteger dim = (NSUInteger)GPU_SVG_ATLAS_SIZE_COLOR;
        NSUInteger tight_bpr = dim * 4u; // RGBA8: four interleaved bytes per texel

        u8* data = arena_alloc_array(arena, u8, (size_t)dim * (size_t)dim * 4u);
        if (!metal_blit_texture_tight(self, self->color_svg_atlas_tex, dim, dim, tight_bpr, data)) {
            return false;
        }

        *out = (GpuColorSvgAtlasReadback) {
            .texels = data,
            .width = (u32)dim,
            .height = (u32)dim,
        };
        return true;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Debug seams with no Metal implementation (gpu_debug.h): the tile-classification readback, the debug render mode,
// the 3D viewport target, and the frame stats report unavailable. Image-atlas residency is answered from the CPU
// placement table.

bool gpu_debug_read_tile_classification(void* renderer_user_data, FlSurface* surface, struct FlArena* arena,
                                        GpuTileClassification* out) {
    (void)renderer_user_data;
    (void)surface;
    (void)arena;
    *out = (GpuTileClassification) { 0 };
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void gpu_debug_set_mode(void* renderer_user_data, GpuDebugMode mode) {
    (void)renderer_user_data;
    (void)mode;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
GpuViewport3dTarget gpu_debug_viewport3d_target(void* renderer_user_data, u32 viewport_id) {
    (void)renderer_user_data;
    (void)viewport_id;
    return (GpuViewport3dTarget) { 0 };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void gpu_debug_read_frame_stats(void* renderer_user_data, FlSurface* surface, struct FlArena* arena,
                                GpuFrameStats* out) {
    (void)renderer_user_data;
    (void)surface;
    (void)arena;
    *out = (GpuFrameStats) { 0 };
}
