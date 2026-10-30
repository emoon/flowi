// Metal GPU render backend: surfaces, the GpuRenderOps table, encoder plumbing, and renderer create/destroy. There
// is no mesh or 3D viewport seam on this backend.

#import <Foundation/Foundation.h>

#include "gpu_metal_internal.h"
#include "gpu_orchestrator.h"   // the shared per-frame sequence + the GpuRenderOps table this backend fills
#include "gpu_bindings.h"       // GPU_BIND_* (the buffer/texture indices this encoder and the MSL signatures share)
#include "gpu_debug.h"          // GPU_TILE_SIZE, GPU_TILE_STRIDE_U32 (the per-tile classification list stride)
#include "gpu_glyph_atlas.h"    // GPU_GLYPH_ATLAS_SIZE (the atlas texture extent) + gpu_glyph_atlas_init
#include "gpu_prepare_core.h"   // gpu_prepare_scale_changed, gpu_prepare_log_slot_overflow
#include "gpu_ir_to_draw_cmd.h" // gpu_ir_to_draw_cmds + the draw-command / AABB / clip arrays the passes consume
#include "gpu_shaders.h"        // the shared compute-shader sources (spliced as MSL via shader_compat_metal.h)
#include "gpu_svg_atlas.h"      // GPU_SVG_ATLAS_SIZE_MONO / _COLOR (the atlas texture extents) + gpu_svg_atlas_init
#include <core/log.h>
#include <string.h> // memcpy (the per-frame SSBO uploads)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The CoreFoundation half of the METAL_RELEASE teardown idiom: drop a +1-owned CFTypeRef and clear its field.
// CFRelease(NULL) is undefined where release on nil is a no-op, so this form keeps a guard.

#define METAL_CF_RELEASE(ref) \
    do {                      \
        if ((ref) != NULL) {  \
            CFRelease((ref)); \
            (ref) = NULL;     \
        }                     \
    } while (0)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// One 2D-texture allocation: a texture of fmt at w x h with the given usage + storage mode, +1 retained. The
// autoreleased descriptor is drained by the caller's @autoreleasepool. Returns nil on failure.
static id<MTLTexture> metal_make_texture(id<MTLDevice> device, MTLPixelFormat fmt, NSUInteger w, NSUInteger h,
                                         MTLTextureUsage usage, MTLStorageMode mode) {
    MTLTextureDescriptor* desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt width:w height:h mipmapped:NO];
    desc.usage = usage;
    desc.storageMode = mode;
    return [device newTextureWithDescriptor:desc]; // +1 retained (owned by the caller)
}

// One row per backend-wide texture create() brings up: where the +1-retained texture lands, its four descriptor
// knobs, and the name a failure is logged under. The per-surface target is not built through this - its failure is
// recoverable, not a backend abort.
typedef struct MetalTextureSpec {
    id<MTLTexture>* out;
    MTLPixelFormat fmt;
    NSUInteger w;
    NSUInteger h;
    MTLTextureUsage usage;
    MTLStorageMode mode;
    const char* label;
} MetalTextureSpec;

// Create every texture in specs. Returns false on the first failure; textures made before it stay on MetalShared
// and are released by the caller's teardown.
static bool metal_make_textures(id<MTLDevice> device, const MetalTextureSpec* specs, size_t count) {
    @autoreleasepool { // drains every descriptor the loop below allocates
        for (size_t i = 0; i < count; i++) {
            const MetalTextureSpec* spec = &specs[i];
            *spec->out = metal_make_texture(device, spec->fmt, spec->w, spec->h, spec->usage,
                                            spec->mode); // +1 retained (owned by MetalShared)
            if (*spec->out == nil) {
                log_error("metal backend: %s texture creation failed", spec->label);
                return false;
            }
        }
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// IOSurface-backed offscreen target for the cross-process accessor: one RGBA8 IOSurface at w x h wrapped in an
// id<MTLTexture> with the same ShaderWrite|ShaderRead usage the private target carries. An IOSurface-backed texture
// cannot use private storage - it is Shared on unified-memory devices and Managed on discrete ones. Both the
// IOSurface (+1) and the texture (+1) are owned by the caller. Returns nil on failure, *out_surface left NULL.
static id<MTLTexture> metal_make_iosurface_texture(id<MTLDevice> device, u32 w, u32 h, IOSurfaceRef* out_surface) {
    *out_surface = NULL;
    // RGBA8 (four bytes/element), matching the target's MTLPixelFormatRGBA8Unorm so a consumer opening the IOSurface
    // as its own MTLTexture reads the same channel order. IOSurface picks the bytes-per-row itself.
    NSDictionary* props = @{
        (id)kIOSurfaceWidth : @(w),
        (id)kIOSurfaceHeight : @(h),
        (id)kIOSurfaceBytesPerElement : @(4),
        (id)kIOSurfacePixelFormat : @((unsigned)'RGBA'),
    };
    IOSurfaceRef surf = IOSurfaceCreate((CFDictionaryRef)props); // +1 (owned)
    if (surf == NULL) {
        return nil;
    }
    MTLTextureDescriptor* desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:(NSUInteger)w
                                                          height:(NSUInteger)h mipmapped:NO];
    desc.usage = MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead;
    desc.storageMode = device.hasUnifiedMemory ? MTLStorageModeShared : MTLStorageModeManaged;
    id<MTLTexture> tex = [device newTextureWithDescriptor:desc iosurface:surf plane:0]; // +1 (owned)
    if (tex == nil) {
        CFRelease(surf);
        return nil;
    }
    *out_surface = surf;
    return tex;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Slot-image tier (the non-atlas image paths). The raster pass declares an array of IMAGE_SLOT_COUNT sampler slots
// starting at texture index GPU_BIND_RASTER_TEX_SLOT0, past the target and the glyph/image/SVG atlases at 0..4. The
// translator hands each referenced slot image a per-frame slot index in the draw command.

// One slot texture: R8 for an alpha-coverage source, RGBA8 for a color source; managed storage, ShaderRead only.
// An alpha-coverage source reads as (r,r,r,r) and a dynamic color buffer is forced opaque (a -> 1). +1 retained
// (owned by the caller). Returns nil on failure.
static id<MTLTexture> metal_make_slot_texture(id<MTLDevice> device, u32 w, u32 h, bool alpha_only, bool force_opaque) {
    MTLPixelFormat fmt = alpha_only ? MTLPixelFormatR8Unorm : MTLPixelFormatRGBA8Unorm;
    MTLTextureDescriptor* desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt width:(NSUInteger)w height:(NSUInteger)h
                                                       mipmapped:NO];
    desc.usage = MTLTextureUsageShaderRead;
    desc.storageMode = MTLStorageModeManaged;
    if (alpha_only) {
        desc.swizzle = MTLTextureSwizzleChannelsMake(MTLTextureSwizzleRed, MTLTextureSwizzleRed, MTLTextureSwizzleRed,
                                                     MTLTextureSwizzleRed);
    } else if (force_opaque) {
        desc.swizzle = MTLTextureSwizzleChannelsMake(MTLTextureSwizzleRed, MTLTextureSwizzleGreen,
                                                     MTLTextureSwizzleBlue, MTLTextureSwizzleOne);
    }
    return [device newTextureWithDescriptor:desc]; // +1 retained (owned by the caller)
}

// Realize a slot texture into the pool: claim a free pool index, create the texture, and hand back the slot
// module's opaque name - the 1-based pool index, keeping 0 as its "unrealized" sentinel. The pool is sized so a
// claim cannot fail within the slot module's table + dead-list bounds, so a full pool means a leak elsewhere.
// Returns 0 with the failure logged when the pool is full or creation fails, leaving the entry unrealized.
u32 metal_slot_realize(MetalShared* self, u32 w, u32 h, bool alpha_only, bool force_opaque) {
    int p = -1;
    for (int i = 0; i < METAL_SLOT_TEX_POOL_CAP; i++) {
        if (self->slot_tex_pool[i] == nil) {
            p = i;
            break;
        }
    }
    if (p < 0) {
        log_error("metal backend: slot texture pool full - %ux%u image dropped", w, h);
        return 0;
    }
    @autoreleasepool { // drains the descriptor metal_make_slot_texture allocates
        self->slot_tex_pool[p] = metal_make_slot_texture(self->device, w, h, alpha_only,
                                                         force_opaque); // +1 owned by the pool
    }
    if (self->slot_tex_pool[p] == nil) {
        log_error("metal backend: %ux%u slot texture creation failed", w, h);
        return 0;
    }
    return (u32)(p + 1);
}

// Upload a source region into a managed texture. bytesPerRow carries the source's row pitch directly, so a padded
// source needs no repack; a 0 stride means tightly packed at bpp bytes per texel.
void metal_texture_upload(id<MTLTexture> tex, u32 x, u32 y, u32 w, u32 h, u32 stride, u32 bpp, const u8* pixels) {
    [tex replaceRegion:MTLRegionMake2D(x, y, w, h)
           mipmapLevel:0
             withBytes:pixels
           bytesPerRow:stride != 0 ? stride : w * bpp];
}

// Drain the slot module's dead-texture list: the u32 names are 1-based pool indices, so release each pooled
// texture and free its slot. The only drain site, called at frame begin after begin_frame queues this frame's
// evictions. Teardown does not drain - destroy releases the whole pool wholesale.
static void metal_slot_drain_dead(MetalShared* self) {
    const u32* dead = NULL;
    u32 count = gpu_image_slots_take_dead(&self->cpu.image_slots, &dead);
    for (u32 i = 0; i < count; i++) {
        u32 tex = dead[i];
        if (tex == 0) {
            continue; // never-realized entry; nothing pooled
        }
        METAL_RELEASE(self->slot_tex_pool[tex - 1]);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// (Re)build everything resolution-dependent: the offscreen RGBA8 target + the CPU readback buffer. Called from
// create_surface and change_resolution. Rewinds to the savepoint just past the struct before reallocating the
// tight readback buffer, so a resize leaves no residue in the per-surface arena.

static void metal_setup_resolution(MetalShared* self, MetalSurface* surface, int width, int height) {
    @autoreleasepool {
        METAL_RELEASE(surface->target);
        METAL_CF_RELEASE(surface->iosurface); // the old backing; a resize reallocates it below when in IOSurface mode
        METAL_RELEASE(surface->tile_buf);

        surface->width = width;
        surface->height = height;
        surface->tile_cols = (width + GPU_TILE_SIZE - 1) / GPU_TILE_SIZE;
        surface->tile_rows = (height + GPU_TILE_SIZE - 1) / GPU_TILE_SIZE;

        // Offscreen render target: RGBA8, one level, ShaderWrite | ShaderRead, private storage unless
        // want_iosurface is latched. A failed IOSurface-backed allocation falls back to private storage and
        // un-latches.
        if (surface->want_iosurface) {
            surface->target =
                metal_make_iosurface_texture(self->device, (u32)width, (u32)height, &surface->iosurface); // +1 owned
            if (surface->target == nil) {
                log_error("metal backend: %dx%d IOSurface-backed target creation failed - falling back to a "
                          "private-storage target (the cross-process accessor is unavailable on this surface)",
                          width, height);
                surface->want_iosurface = false;
                surface->iosurface_failed = true;
            }
        }
        if (surface->target == nil) {
            surface->target = metal_make_texture(self->device, MTLPixelFormatRGBA8Unorm, (NSUInteger)width,
                                                 (NSUInteger)height,
                                                 MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead,
                                                 MTLStorageModePrivate); // +1 retained (owned)
            if (surface->target == nil) {
                log_error("metal backend: %dx%d render target creation failed - this surface cannot render", width,
                          height);
            }
        }

        // Per-tile classification list SSBO, sized to the tile grid: classify writes each tile's
        // [count, overflow, index * TILE_CAPACITY], raster reads it back the same frame. Private - produced and
        // consumed entirely on the GPU.
        NSUInteger tile_bytes = (NSUInteger)surface->tile_cols * (NSUInteger)surface->tile_rows
                                * (NSUInteger)GPU_TILE_STRIDE_U32 * sizeof(u32);
        surface->tile_buf = [self->device newBufferWithLength:tile_bytes
                                                      options:MTLResourceStorageModePrivate]; // +1 retained (owned)
        if (surface->tile_buf == nil) {
            log_error("metal backend: %lu-byte tile buffer creation failed - this surface cannot render",
                      (unsigned long)tile_bytes);
        }

        fl_arena_temp_end(surface->readback_reset); // rewind to just past the struct; drops the old readback buffer
        surface->readback_buf = arena_alloc_array(surface->arena, u8, (u64)width * (u64)height * 4u);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Surface lifecycle: a Metal surface owns the offscreen RGBA8 target and the CPU readback buffer, both in one
// per-surface arena - the struct first, then a readback_reset savepoint past it. destroy_surface releases the
// target texture and frees that arena wholesale. The host calls it before the backend's destroy().

static FlSurface* metal_create_surface(void* user_data, int width, int height) {
    MetalShared* self = user_data;
    FlArena* arena = arena_new();
    MetalSurface* surface = arena_alloc_zero(arena, MetalSurface);
    surface->shared = self; // the IOSurface accessor rebuilds the target and needs the device
    surface->arena = arena;
    surface->readback_reset = fl_arena_temp_begin(arena); // savepoint past the struct; the readback buffer (re)allocs from here
    metal_setup_resolution(self, surface, width, height);
    return (FlSurface*)surface;
}

static void metal_destroy_surface(void* user_data, FlSurface* surface_handle) {
    (void)user_data;
    MetalSurface* surface = (MetalSurface*)surface_handle;
    if (surface == NULL) {
        return;
    }
    METAL_RELEASE(surface->target);
    METAL_CF_RELEASE(surface->iosurface); // the cross-process backing, if the host ever requested it
    METAL_RELEASE(surface->tile_buf);
    fl_arena_destroy(surface->arena); // frees the readback buffer AND the MetalSurface struct itself (dangling after this)
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Neutral RenderInterface seams (vtable).

static void metal_change_resolution(void* user_data, FlSurface* surface_handle, int width, int height) {
    MetalShared* self = user_data;
    MetalSurface* surface = (MetalSurface*)surface_handle;
    if (width == surface->width && height == surface->height) {
        return;
    }
    metal_setup_resolution(self, surface, width, height);
}

// One MTLBuffer holding a per-frame array, uploaded verbatim as a classify/raster SSBO. Shared storage,
// CPU-written and GPU-read; an empty frame gets one zeroed element so the binding is never undersized. +1 owned
// by the caller, released after the command buffer completes. Returns nil on an allocation failure, which every
// caller gates on before encoding its dispatch.
id<MTLBuffer> metal_upload_ssbo(id<MTLDevice> device, const void* data, NSUInteger bytes, NSUInteger stride) {
    NSUInteger len = bytes != 0 ? bytes : stride; // one zeroed element for an empty frame
    id<MTLBuffer> buf = [device newBufferWithLength:len options:MTLResourceStorageModeShared]; // +1 owned
    if (buf == nil) {
        log_error("metal backend: %lu-byte upload buffer creation failed", (unsigned long)len);
        return nil;
    }
    if (bytes != 0) {
        memcpy([buf contents], data, bytes);
    } else {
        memset([buf contents], 0, len);
    }
    return buf;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shared-orchestrator ops: the Metal half of the GpuRenderOps table, driven by gpu_orchestrate_frame in the
// canonical per-frame order. The backend/surface handles arrive as void* and cast back to MetalShared/MetalSurface.

// Per-frame preamble: advance the slot table (evicting stale dynamic entries) and release the textures freed since
// the last render - both before translation, which rebuilds the slot table. Eviction happens only here, never
// mid-translation, where the slot table holds indices into the entry array.
static void metal_op_frame_begin(void* backend) {
    MetalShared* self = backend;
    gpu_image_slots_begin_frame(&self->cpu.image_slots);
    metal_slot_drain_dead(self);
}

static struct FlFontCache* metal_op_get_font_cache(void* backend) {
    return ((MetalShared*)backend)->font_cache;
}

// Translate the neutral IR into the shared draw-command / AABB / clip arrays. The frame envelope's background
// state translates to clear + fullscreen image commands at the front of the stream. NULL/empty-frame safe. Slot
// overflow is logged once per frame and the dropped commands do not render.
static GpuDrawList metal_op_translate(void* backend, const struct FlRenderFrame* frame, u32 screen_w, u32 screen_h) {
    MetalShared* self = backend;
    const void* ir = frame != NULL ? frame->commands : NULL;
    size_t ir_bytes = frame != NULL ? frame->bytes : 0;
    u8 global_alpha = frame != NULL ? frame->global_alpha : 255;
    const struct FlBackground* background = frame != NULL ? &frame->background : NULL;
    GpuDrawList list =
        gpu_ir_to_draw_cmds(self->frame_arena, ir, ir_bytes, global_alpha, screen_w, screen_h, &self->cpu.image_atlas,
                            &self->cpu.glyph_atlas, &self->cpu.image_slots, background, &self->cpu.svg_atlas,
                            &self->cpu.color_svg_atlas);
    gpu_prepare_log_slot_overflow(&self->cpu.image_slots);
    return list;
}

// Open the frame's command encoding: one command buffer + one compute encoder, both autoreleased and held on
// MetalShared for the classify/raster/end ops. A nil command buffer leaves the encoder nil, which skips every
// dispatch below.
static void metal_op_begin_encoder(void* backend, void* surface) {
    (void)surface;
    MetalShared* self = backend;
    self->frame_cmd = [self->queue commandBuffer];
    if (self->frame_cmd == nil) {
        log_error("metal backend: command buffer creation failed - this frame is not encoded (device lost?)");
        self->frame_enc = nil;
        return;
    }
    self->frame_enc = [self->frame_cmd computeCommandEncoder];
    if (self->frame_enc == nil) {
        log_error("metal backend: compute encoder creation failed - this frame is not encoded");
    }
}

// Upload the draw arrays verbatim as the classify/raster input SSBOs, +1 owned and released in end_encoder once
// the command buffer completes. An allocation failure leaves the field nil, which metal_frame_encodable catches.
static void metal_op_upload_inputs(void* backend, const GpuDrawList* list) {
    MetalShared* self = backend;
    if (self->frame_enc == nil) {
        return; // no encoder to feed - do not allocate buffers no dispatch will read
    }
    self->frame_cmd_buf =
        metal_upload_ssbo(self->device, list->cmds, list->count * sizeof(GpuDrawCmd), sizeof(GpuDrawCmd));
    self->frame_aabb_buf =
        metal_upload_ssbo(self->device, list->aabbs, list->count * sizeof(GpuAabb), sizeof(GpuAabb));
    self->frame_clip_buf =
        metal_upload_ssbo(self->device, list->clips, list->clip_count * sizeof(GpuAabb), sizeof(GpuAabb));
}

// Every resource the two dispatches bind comes from an allocation that can fail, each logged at its own site.
// Missing any one means both dispatches skip, so the frame renders nothing rather than half of something.
static bool metal_frame_encodable(const MetalShared* self, const MetalSurface* surf) {
    return self->frame_enc != nil && self->frame_cmd_buf != nil && self->frame_aabb_buf != nil
           && self->frame_clip_buf != nil && surf->target != nil && surf->tile_buf != nil;
}

// Pass 1: classification - one thread per tile, dispatched over ceil(tiles / GPU_TILE_SIZE) threadgroups.
static void metal_op_classify(void* backend, void* surface, u32 cmd_count) {
    MetalShared* self = backend;
    MetalSurface* surf = surface;
    if (!metal_frame_encodable(self, surf)) {
        return;
    }
    u32 tile_cols = (u32)surf->tile_cols;
    u32 tile_rows = (u32)surf->tile_rows;
    MTLSize tile_threads = MTLSizeMake(GPU_TILE_SIZE, GPU_TILE_SIZE, 1); // one thread per tile
    id<MTLComputeCommandEncoder> enc = self->frame_enc;
    [enc setComputePipelineState:self->classify_pipeline];
    [enc setBuffer:self->frame_cmd_buf offset:0 atIndex:GPU_BIND_CLASSIFY_CMDS];
    [enc setBuffer:self->frame_aabb_buf offset:0 atIndex:GPU_BIND_CLASSIFY_AABBS];
    [enc setBuffer:surf->tile_buf offset:0 atIndex:GPU_BIND_CLASSIFY_TILE_LISTS];
    [enc setBytes:&cmd_count length:sizeof(cmd_count) atIndex:GPU_BIND_CLASSIFY_CMD_COUNT];
    [enc setBytes:&tile_cols length:sizeof(tile_cols) atIndex:GPU_BIND_CLASSIFY_TILE_COLS];
    [enc setBytes:&tile_rows length:sizeof(tile_rows) atIndex:GPU_BIND_CLASSIFY_TILE_ROWS];
    MTLSize classify_groups = MTLSizeMake((NSUInteger)(surf->tile_cols + GPU_TILE_SIZE - 1) / GPU_TILE_SIZE,
                                          (NSUInteger)(surf->tile_rows + GPU_TILE_SIZE - 1) / GPU_TILE_SIZE, 1);
    [enc dispatchThreadgroups:classify_groups threadsPerThreadgroup:tile_threads];
}

// The tile lists classify wrote are read as an SSBO by raster: order the two dispatches on the buffer.
static void metal_op_barrier(void* backend) {
    [((MetalShared*)backend)->frame_enc memoryBarrierWithScope:MTLBarrierScopeBuffers];
}

// Bind this frame's assigned slot textures for the raster pass, just before the raster dispatch while the compute
// encoder is open. The raster kernel declares a fixed array of IMAGE_SLOT_COUNT sampler slots at
// GPU_BIND_RASTER_TEX_SLOT0 and Metal requires every element bound: slot_placeholder_tex fills the unassigned
// slots and each assigned slot binds its real texture. A dynamic entry not yet realized (tex == 0) gets its
// texture created on first reference; a dirty dynamic entry streams its content via replaceRegion. The per-slot
// reciprocal extent and filter mode ride alongside as small constant buffers.
static void metal_bind_frame_slot_textures(MetalShared* self) {
    GpuImageSlots* slots = &self->cpu.image_slots;
    id<MTLComputeCommandEncoder> enc = self->frame_enc;
    float slot_inv_size[GPU_IMAGE_SLOT_COUNT * 2];
    u32 slot_filter[GPU_IMAGE_SLOT_COUNT];

    for (u32 i = 0; i < GPU_IMAGE_SLOT_COUNT; i++) {
        slot_inv_size[i * 2 + 0] = 1.0f; // a defined divisor for unassigned slots (never sampled)
        slot_inv_size[i * 2 + 1] = 1.0f;
        slot_filter[i] = 0;
        // The transparent placeholder: unassigned slots are never sampled, and a slot whose realization fails
        // below keeps it, so that command samples transparent (renders nothing) rather than an arbitrary texel.
        [enc setTexture:self->slot_placeholder_tex atIndex:GPU_BIND_RASTER_TEX_SLOT0 + i];
    }

    for (u32 s = 0; s < slots->slot_count; s++) {
        GpuSlotImage* img = &slots->images[slots->slot_image[s]];
        if (img->tex == 0) {
            // Dynamic entry awaiting realization (statics arrive realized from prepare); color buffers are forced
            // opaque.
            img->tex = metal_slot_realize(self, img->w, img->h, img->alpha_only, true);
            if (img->tex == 0) {
                continue; // realization failed (logged there); the placeholder stays bound and renders nothing
            }
        }
        id<MTLTexture> tex = self->slot_tex_pool[img->tex - 1];
        if (img->dirty) {
            metal_texture_upload(tex, 0, 0, img->w, img->h, img->stride, img->alpha_only ? 1u : 4u,
                                 img->pixels); // stream this frame's content
            img->dirty = false;
            self->cpu.dynamic_upload_count++; // debug seam: one tick per dynamic stream, only on referencing frames
        }
        [enc setTexture:tex atIndex:GPU_BIND_RASTER_TEX_SLOT0 + s];
        slot_inv_size[s * 2 + 0] = 1.0f / (float)img->w;
        slot_inv_size[s * 2 + 1] = 1.0f / (float)img->h;
        slot_filter[s] = slots->slot_filter[s];
    }

    [enc setBytes:slot_inv_size length:sizeof(slot_inv_size) atIndex:GPU_BIND_RASTER_SLOT_INV_SIZE];
    [enc setBytes:slot_filter length:sizeof(slot_filter) atIndex:GPU_BIND_RASTER_SLOT_FILTER];
}

// Pass 2: raster - one threadgroup per tile (threadgroup_position_in_grid == gl_WorkGroupID), one thread per
// pixel. Binds the offscreen target as the write image plus every atlas and the slot-image sampler array. The
// raster pipeline declares them all, so all must be bound, though the shader fetches each only for its kind.
static void metal_op_raster(void* backend, void* surface) {
    MetalShared* self = backend;
    MetalSurface* surf = surface;
    if (!metal_frame_encodable(self, surf)) {
        return;
    }
    u32 tile_cols = (u32)surf->tile_cols;
    u32 screen_size[2] = { (u32)surf->width, (u32)surf->height };
    u32 debug_mode = 0; // GpuDebug_Off: the heat-map mode is not selectable on Metal
    MTLSize tile_threads = MTLSizeMake(GPU_TILE_SIZE, GPU_TILE_SIZE, 1);
    id<MTLComputeCommandEncoder> enc = self->frame_enc;
    [enc setComputePipelineState:self->raster_pipeline];
    [enc setBuffer:self->frame_cmd_buf offset:0 atIndex:GPU_BIND_RASTER_CMDS];
    [enc setBuffer:surf->tile_buf offset:0 atIndex:GPU_BIND_RASTER_TILE_LISTS];
    [enc setBuffer:self->frame_clip_buf offset:0 atIndex:GPU_BIND_RASTER_CLIPS];
    [enc setBytes:&tile_cols length:sizeof(tile_cols) atIndex:GPU_BIND_RASTER_TILE_COLS];
    [enc setBytes:screen_size length:sizeof(screen_size) atIndex:GPU_BIND_RASTER_SCREEN_SIZE];
    [enc setBytes:&debug_mode length:sizeof(debug_mode) atIndex:GPU_BIND_RASTER_DEBUG_MODE];
    [enc setTexture:surf->target atIndex:GPU_BIND_RASTER_TEX_TARGET];
    [enc setTexture:self->glyph_atlas_tex atIndex:GPU_BIND_RASTER_TEX_GLYPH];
    [enc setTexture:self->image_atlas_tex atIndex:GPU_BIND_RASTER_TEX_IMAGE];
    [enc setTexture:self->svg_atlas_tex atIndex:GPU_BIND_RASTER_TEX_SVG];
    [enc setTexture:self->color_svg_atlas_tex atIndex:GPU_BIND_RASTER_TEX_SVG_COLOR];
    metal_bind_frame_slot_textures(self); // the KIND_SLOT_IMAGE sampler array at textures 5..15, plus its uniforms
    MTLSize raster_groups = MTLSizeMake((NSUInteger)surf->tile_cols, (NSUInteger)surf->tile_rows, 1);
    [enc dispatchThreadgroups:raster_groups threadsPerThreadgroup:tile_threads];
}

// Close the frame: end encoding, commit, and wait, making the raster observable to a subsequent read_pixels on
// this thread. The command buffer has completed, so the +1 upload buffers are no longer GPU-referenced and are
// released here; the command buffer and encoder are autoreleased.
static void metal_op_end_encoder(void* backend) {
    MetalShared* self = backend;
    [self->frame_enc endEncoding];
    [self->frame_cmd commit];
    [self->frame_cmd waitUntilCompleted];
    METAL_RELEASE(self->frame_cmd_buf);
    METAL_RELEASE(self->frame_aabb_buf);
    METAL_RELEASE(self->frame_clip_buf);
    self->frame_cmd = nil; // autoreleased (drained by the per-render pool), so cleared rather than released
    self->frame_enc = nil;
}

static const GpuRenderOps metal_render_ops = {
    .frame_begin = metal_op_frame_begin,
    .get_font_cache = metal_op_get_font_cache,
    .prepare_glyph = metal_prepare_glyph,
    .translate = metal_op_translate,
    .begin_encoder = metal_op_begin_encoder,
    .upload_inputs = metal_op_upload_inputs,
    .classify = metal_op_classify,
    .barrier = metal_op_barrier,
    .raster = metal_op_raster,
    .end_encoder = metal_op_end_encoder,
};

// Render one frame: the two-dispatch all-compute pipeline, driven by the shared orchestrator through the Metal
// ops above. The @autoreleasepool wraps the whole orchestrate call, so the command buffer and encoder the ops
// create are drained here. A NULL or empty frame still runs both passes: classification zeroes every tile count
// and raster clears the target to transparent.
static void metal_render(void* user_data, FlSurface* surface_handle, const FlRenderFrame* frame) {
    @autoreleasepool {
        MetalShared* self = user_data;
        MetalSurface* surface = (MetalSurface*)surface_handle;
        gpu_orchestrate_frame(&metal_render_ops, self, surface, frame, (u32)surface->width, (u32)surface->height,
                              &self->cpu.glyph_atlas, self->frame_arena, self->perm_arena);
    }
}

static void metal_destroy(void* user_data) {
    MetalShared* self = user_data;
    METAL_RELEASE(self->classify_pipeline);
    METAL_RELEASE(self->raster_pipeline);
    METAL_RELEASE(self->glyph_pipeline);
    METAL_RELEASE(self->color_svg_pipeline);
    METAL_RELEASE(self->glyph_atlas_tex);
    METAL_RELEASE(self->image_atlas_tex);
    METAL_RELEASE(self->svg_atlas_tex);
    METAL_RELEASE(self->color_svg_atlas_tex);
    // Every slot texture still live in the pool, including any queued on the dead list and not drained. The slot
    // module's bookkeeping is in the perm_arena and freed with it by the caller.
    for (int i = 0; i < METAL_SLOT_TEX_POOL_CAP; i++) {
        METAL_RELEASE(self->slot_tex_pool[i]);
    }
    METAL_RELEASE(self->slot_placeholder_tex);
    if (self->frame_arena != NULL) {
        fl_arena_destroy(self->frame_arena);
        self->frame_arena = NULL;
    }
    METAL_RELEASE(self->queue);
    METAL_RELEASE(self->device);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Resolution-independent vtable seams: the UI-scale change and the font-cache bind.

// Forgets every glyph and SVG placement on a UI-scale change; the next frame's prepare pass repopulates the atlases.
// The textures are not cleared.
static void metal_scale_changed(void* user_data, float ui_scale) {
    MetalShared* self = user_data;
    gpu_prepare_scale_changed(&self->last_ui_scale, ui_scale, self->perm_arena,
                              &self->cpu.glyph_atlas, &self->cpu.svg_atlas, &self->cpu.color_svg_atlas);
}

static void metal_set_font_cache(void* user_data, struct FlFontCache* font_cache) {
    MetalShared* self = user_data;
    self->font_cache = font_cache;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shader compile: build one compute pipeline from a shared program's MSL source. gpu_shaders.c assembles the
// source array; newLibraryWithSource: takes a single source, so it is concatenated into one string here. Returns
// nil on any failure, which the caller gates the whole backend on.
static id<MTLComputePipelineState> metal_build_pipeline(id<MTLDevice> device, int (*assemble)(const char**),
                                                        const char* entry_name, const char* label) {
    @autoreleasepool {
        const char* srcs[GPU_SHADER_MAX_SOURCES];
        int count = assemble(srcs);
        NSMutableString* source = [NSMutableString string];
        for (int i = 0; i < count; i++) {
            [source appendString:[NSString stringWithUTF8String:srcs[i]]];
        }

        NSError* error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
        if (library == nil) {
            log_error("metal backend: %s compile failed: %s", label,
                      error != nil ? [[error localizedDescription] UTF8String] : "unknown");
            return nil;
        }
        id<MTLFunction> function = [library newFunctionWithName:[NSString stringWithUTF8String:entry_name]];
        [library release];
        if (function == nil) {
            log_error("metal backend: %s entry point '%s' not found", label, entry_name);
            return nil;
        }
        id<MTLComputePipelineState> pipeline = [device newComputePipelineStateWithFunction:function error:&error]; // +1
        [function release];
        if (pipeline == nil) {
            log_error("metal backend: %s pipeline creation failed: %s", label,
                      error != nil ? [[error localizedDescription] UTF8String] : "unknown");
            return nil;
        }
        return pipeline; // +1 owned by the caller
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Create the backend: bring up the system MTLDevice, a command queue, and the compute pipelines compiled from the
// shared MSL sources. A missing device or queue, or a shader compile failure, returns a zeroed FlRenderBackend
// (render == NULL), which the host must gate on before injecting.

FlRenderBackend create_gpu_renderer(struct FlArena* arena) {
    MetalShared* self = arena_alloc_zero(arena, MetalShared);
    self->perm_arena = arena;

    self->device = MTLCreateSystemDefaultDevice(); // +1 retained (owned; released in destroy)
    if (self->device == nil) {
        log_error("metal backend: no system default MTLDevice");
        return (FlRenderBackend) { 0 };
    }

    self->queue = [self->device newCommandQueue]; // +1 retained (owned)
    if (self->queue == nil) {
        log_error("metal backend: newCommandQueue failed");
        METAL_RELEASE(self->device);
        return (FlRenderBackend) { 0 };
    }

    self->classify_pipeline =
        metal_build_pipeline(self->device, gpu_classify_sources, "classify_main", "classification shader");
    self->raster_pipeline = metal_build_pipeline(self->device, gpu_raster_sources, "raster_main", "raster shader");
    self->glyph_pipeline = metal_build_pipeline(self->device, gpu_glyph_sources, "glyph_main", "glyph raster shader");
    // Multi-color SVG raster: one dispatch per icon, compositing its baked per-shape fills into an RGBA8 cell. The
    // monochrome SVG path reuses glyph_pipeline, so it needs no program of its own.
    self->color_svg_pipeline =
        metal_build_pipeline(self->device, gpu_color_svg_sources, "color_svg_main", "color svg raster shader");
    if (self->classify_pipeline == nil || self->raster_pipeline == nil || self->glyph_pipeline == nil
        || self->color_svg_pipeline == nil) {
        metal_destroy(self);
        return (FlRenderBackend) { 0 };
    }

    // The backend-wide textures, one row each, every one serving every surface. The GPU-written atlases (glyph and
    // both SVG) take private storage and are never cleared - a prepared cell writes its full padded extent,
    // padding ring included. The CPU-uploaded ones (image atlas, slot placeholder) take managed storage so
    // replaceRegion can reach them. A released image-atlas cell's texels linger until a later placement overwrites
    // them; the handle stops resolving, so nothing samples them meanwhile.
    const MetalTextureSpec textures[] = {
        { &self->glyph_atlas_tex, MTLPixelFormatR8Unorm, GPU_GLYPH_ATLAS_SIZE, GPU_GLYPH_ATLAS_SIZE,
          MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead, MTLStorageModePrivate, "glyph atlas" },
        { &self->image_atlas_tex, MTLPixelFormatRGBA8Unorm, GPU_IMAGE_ATLAS_SIZE, GPU_IMAGE_ATLAS_SIZE,
          MTLTextureUsageShaderRead, MTLStorageModeManaged, "image atlas" },
        { &self->slot_placeholder_tex, MTLPixelFormatRGBA8Unorm, 1, 1, MTLTextureUsageShaderRead,
          MTLStorageModeManaged, "slot placeholder" },
        { &self->svg_atlas_tex, MTLPixelFormatR8Unorm, GPU_SVG_ATLAS_SIZE_MONO, GPU_SVG_ATLAS_SIZE_MONO,
          MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead, MTLStorageModePrivate, "monochrome SVG atlas" },
        { &self->color_svg_atlas_tex, MTLPixelFormatRGBA8Unorm, GPU_SVG_ATLAS_SIZE_COLOR, GPU_SVG_ATLAS_SIZE_COLOR,
          MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead, MTLStorageModePrivate, "multi-color SVG atlas" },
    };
    if (!metal_make_textures(self->device, textures, sizeof textures / sizeof textures[0])) {
        metal_destroy(self);
        return (FlRenderBackend) { 0 };
    }

    // The placeholder's one clear texel: an unassigned (or failed) slot samples transparent, so it renders nothing.
    static const u8 transparent_texel[4] = { 0, 0, 0, 0 };
    metal_texture_upload(self->slot_placeholder_tex, 0, 0, 1, 1, 0, 4, transparent_texel);

    // The pure-CPU placement bookkeeping behind those textures. slot_tex_pool is zeroed by the arena_alloc_zero
    // above, so every slot starts free.
    gpu_glyph_atlas_init(&self->cpu.glyph_atlas, self->perm_arena);
    gpu_image_atlas_init(&self->cpu.image_atlas, self->perm_arena);
    gpu_image_slots_init(&self->cpu.image_slots, self->perm_arena);
    gpu_svg_atlas_init(&self->cpu.svg_atlas, self->perm_arena, GpuSvgTexel_R8);
    gpu_svg_atlas_init(&self->cpu.color_svg_atlas, self->perm_arena, GpuSvgTexel_Rgba8);

    self->last_ui_scale = 1.0f; // the first ui_set_scale(1.0) is a no-op

    self->frame_arena = arena_new(); // per-frame IR-translation + per-glyph lowering scratch, rewound each use

    return (FlRenderBackend) {
        .user_data = self,
        .create_surface = metal_create_surface,
        .destroy_surface = metal_destroy_surface,
        .change_resolution = metal_change_resolution,
        .render = metal_render,
        .destroy = metal_destroy,
        .scale_changed = metal_scale_changed,
        .set_font_cache = metal_set_font_cache,
        .prepare_image = metal_prepare_image,
        .release_image = gpu_release_image,
        .prepare_glyph = metal_prepare_glyph,
        .prepare_vector = metal_prepare_vector,
        .read_pixels = metal_read_pixels,
        // Decode results defer to the begin-frame pump; realization runs on the render thread.
        .prepare_image_on_job = false,
    };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Surface-texture seam (gpu_renderer.h): on Metal the handle is the backend-owned, retained id<MTLTexture> of the
// offscreen RGBA8 target, carried as an opaque void*. Two events rebuild target and so invalidate the handle - a
// change_resolution on this surface, and the surface's first gpu_renderer_surface_iosurface call, which upgrades
// the target to IOSurface-backed storage at the unchanged resolution. Later IOSurface calls invalidate nothing.

GpuSurfaceTexture gpu_renderer_surface_texture(FlSurface* surface_handle) {
    MetalSurface* surface = (MetalSurface*)surface_handle;
    return (GpuSurfaceTexture) {
        .handle = (void*)surface->target,
        .width = surface->width,
        .height = surface->height,
    };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// IOSurface variant of the Surface-texture seam (gpu_renderer.h): the cross-process accessor. The first call
// latches the surface into IOSurface mode and rebuilds target as an IOSurface-backed texture at the current
// resolution, after which both accessors view the same resource. That rebuild invalidates any primary handle held
// across this call. Subsequent calls and change_resolution keep the target IOSurface-backed, so the returned
// handle stays valid until the next change_resolution.
//
// If the upgrade fails, metal_setup_resolution has already restored a private-storage target and set
// iosurface_failed; the handle comes back NULL and the surface keeps rendering, same-process from then on.

GpuSurfaceIOSurface gpu_renderer_surface_iosurface(FlSurface* surface_handle) {
    MetalSurface* surface = (MetalSurface*)surface_handle;
    if (!surface->want_iosurface && !surface->iosurface_failed) {
        // Upgrade the target to IOSurface-backed at the current resolution. setup_resolution reallocates the tile
        // and readback buffers as well, at identical sizes.
        surface->want_iosurface = true;
        metal_setup_resolution(surface->shared, surface, surface->width, surface->height);
    }
    return (GpuSurfaceIOSurface) {
        .handle = (void*)surface->iosurface,
        .width = surface->width,
        .height = surface->height,
    };
}
