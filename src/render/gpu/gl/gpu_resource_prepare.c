#include <core/arena.h>
#include <core/log.h>
#include "gpu_prepare_core.h"
#include "gpu_renderer_internal.h"
#include "gpu_svg_color_lower.h"
#include <ui/font_cache.h>
#include <ui/image_registry.h>
#include <ui/vector_image.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Image preparation seam: place a decoded static image into the atlas, or onto a dedicated slot-tier texture
// (gpu_prepare_image_wants_slot_tier). Runs on the render thread.
//
// Both tiers store premultiplied texels. The atlas holds 1-channel sources expanded to white x alpha RGBA; a
// slot texture keeps them R8 and swizzles (r, r, r, r).

// Realize a decoded image on its own texture and register it with the slot bookkeeping. Returns true once the
// image is on its slot texture (the caller acks the registry only then).
static bool prepare_large_image(GpuShared* self, FlImageHandle handle, const FlImageDecodeResult* decoded,
                                FlArena* scratch) {
    u32 w = (u32)decoded->width;
    u32 h = (u32)decoded->height;
    if (decoded->width > self->max_texture_size || decoded->height > self->max_texture_size) {
        log_error("gpu backend: %ux%u image exceeds GL_MAX_TEXTURE_SIZE (%d) - dropped", w, h,
                  (int)self->max_texture_size);
        return false;
    }

    bool alpha_only = decoded->channels == 1;
    GLuint tex = create_slot_texture(&self->gl, w, h, alpha_only, false);
    tex_sub_image_strided(scratch, 0, 0, w, h, (u32)decoded->stride, alpha_only ? 1u : 4u,
                          alpha_only ? GL_RED : GL_RGBA, decoded->pixels);

    if (gpu_image_slots_register_static(&self->cpu.image_slots, handle, w, h, (u32)tex) < 0) {
        glDeleteTextures(1, &tex); // table full or duplicate - register logged the reason
        return false;
    }
    return true;
}

void prepare_image(void* user_data, FlImageLoadRequestId request_id, const FlImageDecodeResult* decoded,
                   FlArena* scratch, const FlImageLoadRequest* request) {
    GpuShared* self = user_data;

    if (!gpu_prepare_image_decode_valid(request_id, decoded, request)) {
        return;
    }

    if (gpu_prepare_image_wants_slot_tier(decoded, request)) {
        if (prepare_large_image(self, (FlImageHandle)request_id, decoded, scratch)) {
            image_registry_mark_realized((ImageHandle)request_id, decoded->width, decoded->height);
        }
        return;
    }

    GpuAtlasImageUpload upload;
    if (!gpu_prepare_image_place_atlas(&self->cpu.image_atlas, request_id, decoded, scratch, &upload)) {
        return;
    }
    glBindTexture(GL_TEXTURE_2D, self->atlas_tex);
    tex_sub_image_strided(scratch, upload.x, upload.y, upload.w, upload.h, upload.stride, 4, GL_RGBA, upload.pixels);

    image_registry_mark_realized((ImageHandle)request_id, decoded->width, decoded->height);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Cell rasterization shared by glyph and SVG preparation.

// Upload quad_count quadratics into the curve SSBO and bind it at base 0.
static void upload_curves(GpuShared* self, const f32* floats, u32 quad_count) {
    const GpuGl* gl = &self->gl;
    gl->BindBuffer(GL_SHADER_STORAGE_BUFFER, self->curve_ssbo);
    gl->BufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)((size_t)quad_count * 6 * sizeof(f32)), floats,
                   GL_STREAM_DRAW);
    gl->BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, self->curve_ssbo);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Launch one thread per texel of a pw x ph cell, then fence the cell's image stores for texture fetches.
static void dispatch_cell(const GpuGl* gl, u32 pw, u32 ph) {
    gl->DispatchCompute((pw + GPU_TILE_SIZE - 1u) / GPU_TILE_SIZE, (ph + GPU_TILE_SIZE - 1u) / GPU_TILE_SIZE, 1);
    gl->MemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Rasterize quad_count quadratics into the cell at (x, y) of the R8 atlas texture target through glyph_prog.
static void raster_coverage_cell(GpuShared* self, GLuint target, u32 x, u32 y, u32 pw, u32 ph, const f32* floats,
                                 u32 quad_count) {
    const GpuGl* gl = &self->gl;
    upload_curves(self, floats, quad_count);
    gl->UseProgram(self->glyph_prog);
    gl->Uniform2ui(self->u_glyph_cell_origin, x, y);
    gl->Uniform2ui(self->u_glyph_cell_size, pw, ph);
    gl->Uniform1ui(self->u_glyph_seg_count, quad_count);
    gl->BindImageTexture(0, target, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R8);
    dispatch_cell(gl, pw, ph);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Glyph preparation seam: place the glyph and rasterize its lowered outline into the R8 glyph atlas with one
// compute dispatch. Runs on the render thread.

void prepare_glyph(void* user_data, FlFontId font, u16 size, u32 glyph_id, const struct FlGlyphOutline* outline) {
    GpuShared* self = user_data;
    GpuGlyphRaster glyph;
    if (!gpu_prepare_glyph_place(&self->cpu.glyph_atlas, self->frame_arena, font, size, glyph_id, outline, &glyph)) {
        return;
    }

    raster_coverage_cell(self, self->glyph_atlas_tex, glyph.entry->cell.x, glyph.entry->cell.y, glyph.cell.pw,
                         glyph.cell.ph, glyph.floats, glyph.quad_count);
    self->glyph_dispatch_count++;

    fl_arena_rewind(self->frame_arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Rasterize one live SVG cell into its atlas cell: the GpuRasterCellFn the shared vector-prepare calls for a
// cell's first raster and for each survivor's re-raster after a compaction. A monochrome cell (color false)
// rasterizes R8 coverage through glyph_prog into the mono atlas; a multi-color cell composites per-shape records
// through color_svg_prog into the RGBA8 atlas. The frame arena backs the lowered geometry and is rewound here.

static void raster_svg_cell(void* backend, const GpuSvgEntry* entry, const FlVectorImage* vector, bool color) {
    GpuShared* self = backend;
    GpuSvgCellGeometry geom;
    if (!gpu_prepare_svg_cell_geometry(self->frame_arena, entry, vector, color, &geom)) {
        return;
    }

    u32 pw = entry->cell.w;
    u32 ph = entry->cell.h;
    const GpuGl* gl = &self->gl;

    if (color) {
        upload_curves(self, geom.floats, geom.quad_count);
        gl->BindBuffer(GL_SHADER_STORAGE_BUFFER, self->shapes_ssbo);
        gl->BufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)((size_t)geom.shape_count * sizeof(GpuColorShape)),
                       geom.shapes, GL_STREAM_DRAW);
        gl->BindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, self->shapes_ssbo);

        gl->UseProgram(self->color_svg_prog);
        gl->Uniform2ui(self->u_color_svg_cell_origin, entry->cell.x, entry->cell.y);
        gl->Uniform2ui(self->u_color_svg_cell_size, pw, ph);
        gl->Uniform1ui(self->u_color_svg_shape_count, geom.shape_count);
        gl->BindImageTexture(0, self->color_svg_atlas_tex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
        dispatch_cell(gl, pw, ph);
    } else {
        raster_coverage_cell(self, self->svg_atlas_tex, entry->cell.x, entry->cell.y, pw, ph, geom.floats,
                             geom.quad_count);
    }
    self->cpu.svg_raster_count++;

    fl_arena_rewind(self->frame_arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Vector preparation seam: rasterize a solid-fill icon into the mono or multi-color SVG atlas through
// gpu_prepare_vector. Runs on the render thread.

void prepare_vector(void* user_data, FlImageHandle handle, u16 size, const FlVectorImage* vector) {
    GpuShared* self = user_data;
    gpu_prepare_vector(self, &self->cpu.svg_atlas, &self->cpu.color_svg_atlas, handle, size, vector, raster_svg_cell,
                       &self->cpu.svg_compact_count);
}
