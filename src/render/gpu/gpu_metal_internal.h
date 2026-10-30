#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Metal GPU backend internal seam: the MetalShared / MetalSurface state shared by the backend's .m files. Built
// without ARC: long-lived id<> fields are +1 retained; frame_cmd / frame_enc are autoreleased.

#import <Metal/Metal.h>
#import <IOSurface/IOSurface.h>

#include "gpu_cpu_state.h"
#include <stddef.h>
#include "gpu_glyph_atlas.h"
#include "gpu_image_atlas.h"
#include "gpu_image_slots.h"
#include "gpu_svg_atlas.h"
#include "gpu_renderer.h"
#include <core/arena.h>
#include <core/types.h>

struct FlFontCache;
struct FlRenderFrame;

// Drop one +1-owned Objective-C resource and clear its field, so a second teardown pass cannot double-release and
// "is this live?" is a nil check. release on nil is a no-op, so no guard is needed. The CoreFoundation form is not
// symmetric - CFRelease(NULL) is undefined.

#define METAL_RELEASE(obj) \
    do {                   \
        [(obj) release];   \
        (obj) = nil;       \
    } while (0)

// The slot-image tier stores a u32 opaque texture name per entry (a GLuint on GL). Metal textures are objects, not
// integers, so gpu_metal keeps the id<MTLTexture>s in a pool and stores a 1-based pool index there, 0 staying
// "unrealized". The pool must hold every live entry's texture (up to GPU_IMAGE_SLOT_TABLE_CAP) plus every texture
// on the dead list awaiting the next frame's drain (bounded at GPU_IMAGE_SLOT_TABLE_CAP*2).
#define METAL_SLOT_TEX_POOL_CAP (GPU_IMAGE_SLOT_TABLE_CAP * 3)

// The assumed 2D texture cap for the slot tier: a source past this is rejected up front by the prepare TU. The
// device is never queried for its real limit, so this is a floor, not a fact - on a device below it, texture
// creation fails instead and metal_slot_realize logs it and reports the entry unrealized.
#define METAL_SLOT_MAX_TEXTURE_DIM 16384

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Backend-shared tier: the system MTLDevice and one command queue drive every surface - no window, no context.
// Lives in the backend-lifetime perm_arena; destroy() releases the two objects it owns.

typedef struct MetalShared {
    GpuCpuState cpu; // first member: the backend user_data doubles as the GpuCpuState the shared seams read
    id<MTLDevice> device;      // system default device, +1 retained (owned)
    id<MTLCommandQueue> queue; // one queue for the compute frame + readback blits, +1 retained (owned)
    FlArena* perm_arena;       // backend lifetime

    // Compute pipelines for the two-dispatch frame: classification then raster, built once at create from the
    // shared MSL sources. +1 retained (owned); released in destroy.
    id<MTLComputePipelineState> classify_pipeline;
    id<MTLComputePipelineState> raster_pipeline;
    // Glyph coverage raster: the one-dispatch-per-glyph program that lowers an outline's quadratics into an R8
    // atlas cell, plus that backend-wide atlas texture. +1 retained (owned); released in destroy.
    id<MTLComputePipelineState> glyph_pipeline;
    id<MTLTexture> glyph_atlas_tex; // GPU_GLYPH_ATLAS_SIZE^2 R8, shader read + write; never cleared

    // Backend-wide RGBA8 image atlas, sampled by the core-tier image path. Managed storage - prepare_image
    // CPU-uploads cells via replaceRegion. +1 retained (owned); released in destroy.
    id<MTLTexture> image_atlas_tex;

    // SVG vector-icon raster: a monochrome icon reuses glyph_pipeline above, a multi-color icon runs the color_svg
    // program. Both atlases are backend-wide and GPU-written by their prepare dispatch - private storage,
    // ShaderWrite + ShaderRead, never cleared, since every prepared cell writes its full padded extent. +1
    // retained (owned); released in destroy.
    id<MTLComputePipelineState> color_svg_pipeline; // multi-color composite; the mono path shares glyph_pipeline
    id<MTLTexture> svg_atlas_tex;                   // GPU_SVG_ATLAS_SIZE_MONO^2 R8 monochrome/coverage atlas
    id<MTLTexture> color_svg_atlas_tex;             // GPU_SVG_ATLAS_SIZE_COLOR^2 RGBA8 baked multi-color atlas


    // Slot-image tier: large statics past GPU_IMAGE_ATLAS_MAX_DIM, each on its own texture, and dynamic buffers
    // streamed to a dedicated texture on referencing frames. slot_tex_pool holds the id<MTLTexture>s the shared
    // module's u32 tex field indexes 1-based. Realized on demand by prepare_image (statics) or the raster op
    // (dynamics), released via the dead list drained at frame begin.
    id<MTLTexture> slot_tex_pool[METAL_SLOT_TEX_POOL_CAP]; // pool[tex-1] == the entry's texture (nil = free)
    // A 1x1 transparent texture bound to every slot-array element without a realized texture this frame. The
    // raster kernel declares the whole IMAGE_SLOT_COUNT array, so unassigned slots and the realization-failure
    // fallback sample transparent rather than an arbitrary texel. +1 retained (owned).
    id<MTLTexture> slot_placeholder_tex;
    struct FlFontCache* font_cache; // bound via set_font_cache; prepare_text_glyphs pulls outlines from it
    float last_ui_scale;          // scale_changed no-ops on an unchanged scale (seeded to 1.0 at create)

    FlArena* frame_arena; // per-frame IR-translation + per-glyph lowering scratch, rewound each use

    // Per-frame command-encoding state, live only between the orchestrator's begin_encoder and end_encoder ops.
    // The command buffer and compute encoder are autoreleased, drained by the per-render @autoreleasepool wrapping
    // gpu_orchestrate_frame; the three upload buffers are +1 owned and released in end_encoder once the command
    // buffer completes. One frame is in flight at a time, so single-slot fields suffice.
    id<MTLCommandBuffer> frame_cmd;
    id<MTLComputeCommandEncoder> frame_enc;
    id<MTLBuffer> frame_cmd_buf;  // the draw-command SSBO (classify + raster input)
    id<MTLBuffer> frame_aabb_buf; // the per-command AABB SSBO (classify input)
    id<MTLBuffer> frame_clip_buf; // the clip-rect SSBO (raster input)
} MetalShared;
_Static_assert(offsetof(MetalShared, cpu) == 0, "cpu must be the first member");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Per-surface tier: the offscreen RGBA8 target the backend clears into plus the CPU readback buffer, sized to this
// surface's resolution. One arena backs both this struct and the readback buffer; setup_resolution rewinds to
// readback_reset to reallocate the buffer, leaving the struct intact.

typedef struct MetalSurface {
    struct MetalShared* shared;   // owning backend; the IOSurface accessor needs the device to rebuild the target
    FlArena* arena;               // per-surface; backs this struct + the readback buffer, destroyed in destroy_surface
    FlTempArena readback_reset;   // savepoint past this struct; setup_resolution rewinds here before (re)allocating
    id<MTLTexture> target;        // offscreen RGBA8 render target, +1 retained (owned); the void* surface handle
    id<MTLBuffer> tile_buf;       // per-tile classification list, sized to this surface's tile grid, +1 retained (owned)
    // Optional IOSurface backing for the cross-process accessor. want_iosurface latches true on the host's first
    // gpu_renderer_surface_iosurface call, which upgrades target to an IOSurface-backed texture; iosurface then
    // holds that texture's backing IOSurfaceRef (+1, owned; CFReleased on resize/destroy). The latch keeps
    // setup_resolution rebuilding an IOSurface-backed target across change_resolution.
    //
    // If an IOSurface-backed allocation fails, setup_resolution falls back to a private-storage target and clears
    // want_iosurface again. iosurface_failed records that refusal for the surface's life, so the accessor reports a
    // NULL handle instead of re-latching and tearing down the working private target on every call.
    bool want_iosurface;
    bool iosurface_failed;
    IOSurfaceRef iosurface;
    i32 width;                    // current resolution (tracks change_resolution)
    i32 height;
    i32 tile_cols;                // ceil(width / GPU_TILE_SIZE); the classify/raster dispatch grid
    i32 tile_rows;                // ceil(height / GPU_TILE_SIZE)
    u8* readback_buf;             // tight interleaved RGBA8, width*height*4, reused across readbacks
} MetalSurface;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shared Metal upload helpers.

// Realize one slot-tier texture into the pool: claim a free pool index and create the texture. Alpha-coverage
// sources are R8 swizzled to (r, r, r, r); color sources are RGBA8, force_opaque swizzling alpha to 1 for dynamic
// color buffers. Managed storage, so the source CPU-uploads via metal_texture_upload. Returns the slot module's
// opaque texture name - the 1-based pool index, so pool[name - 1] is the texture - or 0 for unrealized when the
// pool is full or creation failed (both logged).
u32 metal_slot_realize(MetalShared* self, u32 w, u32 h, bool alpha_only, bool force_opaque);

// Upload a source region into a managed texture. stride is the source's row pitch in bytes, 0 meaning tightly
// packed (w * bpp); Metal's bytesPerRow carries a padded pitch directly, so no repack is ever needed. bpp is
// the source's bytes per texel: 1 for an R8 alpha-coverage source, 4 for RGBA8.
void metal_texture_upload(id<MTLTexture> tex, u32 x, u32 y, u32 w, u32 h, u32 stride, u32 bpp, const u8* pixels);

// One MTLBuffer holding a per-frame array, uploaded verbatim as a classify/raster/curve SSBO. Shared storage; an
// empty array gets one zeroed element so the binding is never undersized. +1 owned by the caller, released once
// the dispatch completes. Returns nil on an allocation failure (logged there), on which every caller skips its
// dispatch.
id<MTLBuffer> metal_upload_ssbo(id<MTLDevice> device, const void* data, NSUInteger bytes, NSUInteger stride);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Resource-prepare seams (gpu_resource_prepare_metal.m): image / glyph / SVG realization.

void metal_prepare_image(void* user_data, FlImageLoadRequestId request_id, const FlImageDecodeResult* decoded,
                         FlArena* scratch, const FlImageLoadRequest* request);
void metal_prepare_glyph(void* user_data, FlFontId font, uint16_t size, uint32_t glyph_id,
                         const FlGlyphOutline* outline);
void metal_prepare_vector(void* user_data, FlImageHandle handle, uint16_t size, const FlVectorImage* vector);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pixel-readback seam (render_backend.h): defined in gpu_debug_readback_metal.m. Blits the offscreen target into
// a storageModeShared MTLBuffer and copies it into the surface's readback buffer.

bool metal_read_pixels(void* renderer_user_data, FlSurface* surface, FlPixelReadback* out);
