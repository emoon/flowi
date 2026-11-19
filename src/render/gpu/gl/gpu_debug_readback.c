#include <core/arena.h>
#include "gpu_renderer_internal.h"

// Debug seams (gpu_debug.h): verbatim CPU readback of the classification pass's tile lists, and the raster
// debug-mode switch.

bool gpu_debug_read_tile_classification(void* renderer_user_data, FlSurface* surface_handle, struct FlArena* arena,
                                        GpuTileClassification* out) {
    GpuShared* self = renderer_user_data;
    GpuSurface* surface = (GpuSurface*)surface_handle;
    const GpuGl* gl = &self->gl;

    size_t tile_list_bytes
        = (size_t)surface->tile_cols * (size_t)surface->tile_rows * GPU_TILE_STRIDE_U32 * sizeof(u32);
    u32* data = arena_alloc_array(arena, u32, tile_list_bytes / sizeof(u32));

    // render() fences the classification pass's writes for shader reads only; CPU buffer readback is a
    // different access class.
    gl->MemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    gl->BindBuffer(GL_SHADER_STORAGE_BUFFER, surface->tile_ssbo);
    gl->GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, (GLsizeiptr)tile_list_bytes, data);

    *out = (GpuTileClassification) {
        .tile_lists = data,
        .tile_cols = surface->tile_cols,
        .tile_rows = surface->tile_rows,
    };
    return true;
}

void gpu_debug_set_mode(void* renderer_user_data, GpuDebugMode mode) {
    GpuShared* self = renderer_user_data;
    self->debug_mode = mode;
}

typedef struct AtlasReadSpec {
    GLuint tex;
    u32 size;
    GLenum format;
    u32 bytes_per_texel;
} AtlasReadSpec;

// The atlas texels verbatim. The producing dispatch fences its image stores for shader fetches only; CPU
// texture readback is a different access class.

static GpuAtlasReadback read_atlas(const GpuShared* self, struct FlArena* arena, AtlasReadSpec spec) {
    const GpuGl* gl = &self->gl;
    u8* data = arena_alloc_array(arena, u8, (size_t)spec.size * spec.size * spec.bytes_per_texel);
    gl->MemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT);
    glBindTexture(GL_TEXTURE_2D, spec.tex);
    glPixelStorei(GL_PACK_ALIGNMENT, (GLint)spec.bytes_per_texel);
    glGetTexImage(GL_TEXTURE_2D, 0, spec.format, GL_UNSIGNED_BYTE, data);
    return (GpuAtlasReadback) { .texels = data, .width = spec.size, .height = spec.size };
}

// Glyph-atlas debug seams (gpu_debug.h): the R8 coverage texels verbatim.

bool gpu_debug_read_glyph_atlas(void* renderer_user_data, struct FlArena* arena, GpuGlyphAtlasReadback* out) {
    const GpuShared* self = renderer_user_data;
    *out = read_atlas(self, arena, (AtlasReadSpec) { self->glyph_atlas_tex, GPU_GLYPH_ATLAS_SIZE, GL_RED, 1 });
    return true;
}

// SVG-atlas debug seams (gpu_debug.h): the R8 coverage texels verbatim.

bool gpu_debug_read_svg_atlas(void* renderer_user_data, struct FlArena* arena, GpuSvgAtlasReadback* out) {
    const GpuShared* self = renderer_user_data;
    *out = read_atlas(self, arena, (AtlasReadSpec) { self->svg_atlas_tex, GPU_SVG_ATLAS_SIZE_MONO, GL_RED, 1 });
    return true;
}

// Multi-color SVG atlas debug seams: the RGBA8 color texels verbatim.

bool gpu_debug_read_color_svg_atlas(void* renderer_user_data, struct FlArena* arena, GpuColorSvgAtlasReadback* out) {
    const GpuShared* self = renderer_user_data;
    *out = read_atlas(self, arena, (AtlasReadSpec) { self->color_svg_atlas_tex, GPU_SVG_ATLAS_SIZE_COLOR, GL_RGBA, 4 });
    return true;
}

GpuViewport3dTarget gpu_debug_viewport3d_target(void* renderer_user_data, u32 viewport_id) {
    GpuShared* self = renderer_user_data;
    GpuViewport3dTarget result = { .viewport_count = self->viewport3d_count };
    for (u32 i = 0; i < self->viewport3d_count; i++) {
        if (self->viewport3d[i].id == viewport_id) {
            const GpuViewport3d* viewport = &self->viewport3d[i];
            result.found = true;
            result.width = viewport->width;
            result.height = viewport->height;
            result.fbo = viewport->fbo;
            result.depth_rb = viewport->depth_rb;
            int slot = gpu_image_slots_find_static(&self->cpu.image_slots, viewport->color);
            result.color_tex = slot >= 0 ? self->cpu.image_slots.images[slot].tex : 0;
            break;
        }
    }
    return result;
}

// Tier-1 perf-fence work counters (gpu_debug.h): the glyph/SVG dispatch totals off the backend plus the
// surface's summed tile overflow.

void gpu_debug_read_frame_stats(void* renderer_user_data, FlSurface* surface_handle, struct FlArena* arena,
                                GpuFrameStats* out) {
    GpuShared* self = renderer_user_data;

    GpuTileClassification cls;
    gpu_debug_read_tile_classification(renderer_user_data, surface_handle, arena, &cls);
    u32 tile_overflow = 0;
    for (int ty = 0; ty < cls.tile_rows; ty++) {
        for (int tx = 0; tx < cls.tile_cols; tx++) {
            tile_overflow += gpu_tile_overflow(&cls, tx, ty);
        }
    }

    *out = (GpuFrameStats) {
        .glyph_dispatches = self->glyph_dispatch_count,
        .svg_dispatches = self->cpu.svg_raster_count,
        .tile_overflow = tile_overflow,
    };
}
