#include <core/arena.h>
#include <core/log.h>
#include <core/perf_scope.h>
#include "gpu_ir_to_draw_cmd.h"
#include "gpu_orchestrator.h"
#include "gpu_prepare_core.h"
#include "gpu_renderer_internal.h"
#include "gpu_shaders.h"
#include <string.h>

// Texture-unit budget (the GL 4.3 floor guarantees 16 for compute): unit 0 is the scratch bind point every
// upload path uses (and the glyph coverage atlas at raster time), units 1 and 2 the image atlas's two filter
// modes, units 3..GPU_IMAGE_SLOT_UNIT0+GPU_IMAGE_SLOT_COUNT-1 (3..13) the image sampler slots, the unit just
// past them (14) the R8 SVG coverage atlas, and the next (15) the RGBA8 multi-color SVG atlas - the last unit
// within the 16-unit floor.
#define GPU_IMAGE_SLOT_UNIT0 3
#define GPU_SVG_ATLAS_UNIT (GPU_IMAGE_SLOT_UNIT0 + GPU_IMAGE_SLOT_COUNT)
#define GPU_COLOR_SVG_ATLAS_UNIT (GPU_SVG_ATLAS_UNIT + 1)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

GLuint gpu_compile_shader(const GpuGl* gl, GLenum stage, const char* const* sources, int count, const char* name) {
    GLuint shader = gl->CreateShader(stage);
    gl->ShaderSource(shader, count, sources, NULL);
    gl->CompileShader(shader);

    GLint ok = 0;
    gl->GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        gl->GetShaderInfoLog(shader, sizeof(log), NULL, log);
        log_error("gpu backend: %s compile failed: %s", name, log);
        gl->DeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint compile_program(const GpuGl* gl, const char* const* sources, int count, const char* name) {
    GLuint shader = gpu_compile_shader(gl, GL_COMPUTE_SHADER, sources, count, name);
    if (shader == 0) {
        return 0;
    }

    GLuint prog = gl->CreateProgram();
    gl->AttachShader(prog, shader);
    gl->LinkProgram(prog);
    gl->DeleteShader(shader); // program holds its own reference

    GLint ok = 0;
    gl->GetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        gl->GetProgramInfoLog(prog, sizeof(log), NULL, log);
        log_error("gpu backend: %s link failed: %s", name, log);
        gl->DeleteProgram(prog);
        return 0;
    }
    return prog;
}

// One image sampler object: filter (GL_LINEAR / GL_NEAREST) on both min and mag, clamp-to-edge on both axes.
static GLuint create_sampler(const GpuGl* gl, GLint filter) {
    GLuint sampler;
    gl->GenSamplers(1, &sampler);
    gl->SamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, filter);
    gl->SamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, filter);
    gl->SamplerParameteri(sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->SamplerParameteri(sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return sampler;
}

// Empty-frame backstop: size an SSBO to a single zeroed element so binding it before the first upload (a
// frame with no eligible commands) is never an undersized binding. zero points at a zeroed element of
// size bytes - its content is never read on such a frame (classification sees cmd_count 0), only its size.
static void seed_empty_ssbo(const GpuGl* gl, GLuint ssbo, GLsizeiptr size, const void* zero) {
    gl->BindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
    gl->BufferData(GL_SHADER_STORAGE_BUFFER, size, zero, GL_STREAM_DRAW);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// (Re)build everything resolution-dependent: the offscreen target + its readback FBO, the per-tile list SSBO,
// and the CPU readback buffer. Called from create and change_resolution.

static void setup_resolution(GpuShared* self, GpuSurface* surface, int width, int height) {
    const GpuGl* gl = &self->gl;

    if (surface->target_tex != 0) {
        glDeleteTextures(1, &surface->target_tex);
        gl->DeleteFramebuffers(1, &surface->readback_fbo);
    }

    surface->screen_width = width;
    surface->screen_height = height;
    surface->tile_cols = (width + GPU_TILE_SIZE - 1) / GPU_TILE_SIZE;
    surface->tile_rows = (height + GPU_TILE_SIZE - 1) / GPU_TILE_SIZE;

    // Immutable storage: imageStore targets must be texture-complete with exactly one level.
    glGenTextures(1, &surface->target_tex);
    glBindTexture(GL_TEXTURE_2D, surface->target_tex);
    gl->TexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);

    gl->GenFramebuffers(1, &surface->readback_fbo);
    gl->BindFramebuffer(GL_FRAMEBUFFER, surface->readback_fbo);
    gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, surface->target_tex, 0);
    if (gl->CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        log_error("gpu backend: readback framebuffer incomplete");
    }
    gl->BindFramebuffer(GL_FRAMEBUFFER, 0);

    size_t tile_list_bytes
        = (size_t)surface->tile_cols * (size_t)surface->tile_rows * GPU_TILE_STRIDE_U32 * sizeof(u32);
    gl->BindBuffer(GL_SHADER_STORAGE_BUFFER, surface->tile_ssbo);
    gl->BufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)tile_list_bytes, NULL, GL_DYNAMIC_COPY);

    fl_arena_temp_end(
        surface->readback_reset); // rewind to just past the struct; keeps the GpuSurface, drops the old buffer
    surface->readback_buf = arena_alloc_array(surface->arena, u8, (u64)width * (u64)height * 4u);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Surface lifecycle: a GPU surface owns the offscreen RGBA8 target, its readback FBO, the per-tile
// classification SSBO sized to its tile grid, and the CPU readback buffer, all in one per-surface arena - the
// struct first, then a readback_reset savepoint past it. destroy_surface tears down the GL objects and frees that
// arena wholesale. The host calls it before the backend's destroy().
static FlSurface* gpu_create_surface(void* user_data, int width, int height) {
    GpuShared* self = user_data;
    FlArena* arena = arena_new();
    GpuSurface* surface = arena_alloc_zero(arena, GpuSurface);
    surface->arena = arena;
    surface->readback_reset
        = fl_arena_temp_begin(arena); // savepoint past the struct; the readback buffer (re)allocs from here
    self->gl.GenBuffers(1, &surface->tile_ssbo);
    setup_resolution(self, surface, width, height);
    return (FlSurface*)surface;
}

static void gpu_destroy_surface(void* user_data, FlSurface* surface_handle) {
    GpuShared* self = user_data;
    GpuSurface* surface = (GpuSurface*)surface_handle;
    if (surface == nullptr) {
        return;
    }
    const GpuGl* gl = &self->gl;
    glDeleteTextures(1, &surface->target_tex);
    gl->DeleteFramebuffers(1, &surface->readback_fbo);
    gl->DeleteBuffers(1, &surface->tile_ssbo);
    fl_arena_destroy(
        surface->arena); // frees the readback buffer AND the GpuSurface struct itself (dangling after this)
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void change_resolution(void* user_data, FlSurface* surface_handle, int width, int height) {
    GpuShared* self = user_data;
    GpuSurface* surface = (GpuSurface*)surface_handle;
    if (width == surface->screen_width && height == surface->screen_height) {
        return;
    }
    // Framebuffer/tile-list resize only; the atlas-regen trigger is the scale_changed seam below.
    setup_resolution(self, surface, width, height);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Atlas-regen seam, called by the core from ui_set_scale on every absolute UI-scale change: forgets every glyph
// and SVG placement so the live set repacks at the new sizes. The textures are not cleared.
static void scale_changed(void* user_data, float ui_scale) {
    GpuShared* self = user_data;
    gpu_prepare_scale_changed(&self->last_ui_scale, ui_scale, self->perm_arena, &self->cpu.glyph_atlas,
                              &self->cpu.svg_atlas, &self->cpu.color_svg_atlas);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sampler-slot textures (gpu_image_slots.h): the GL half of the non-atlas image paths. Dynamic textures are
// created lazily here on the render thread; their content streams via plain glTexSubImage2D on frames a
// command references them. Texture updates are ordered against the dispatch by the GL command stream, so no
// barrier is needed.

// Delete textures freed off the GL path (release_image, dynamic eviction, storage-shape changes).
static void delete_dead_slot_textures(GpuShared* self) {
    const u32* dead;
    u32 count = gpu_image_slots_take_dead(&self->cpu.image_slots, &dead);
    if (count > 0) {
        glDeleteTextures((GLsizei)count, dead);
    }
}

// Create a slot-tier texture and leave it bound on the active unit. Alpha-coverage sources are R8 swizzled
// to (r, r, r, r), the premultiplied white-x-alpha form. Color sources are RGBA8; force_opaque swizzles alpha to
// 1 for dynamic color buffers. Immutable storage, one level.
GLuint create_slot_texture(const GpuGl* gl, u32 w, u32 h, bool alpha_only, bool force_opaque) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    gl->TexStorage2D(GL_TEXTURE_2D, 1, alpha_only ? GL_R8 : GL_RGBA8, (GLsizei)w, (GLsizei)h);
    if (alpha_only) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_RED);
    } else if (force_opaque) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);
    }
    return tex;
}

// Upload possibly-padded rows into the texture bound on the active unit: rows ride GL_UNPACK_ROW_LENGTH
// when the stride is a whole number of pixels, the rare remainder repacks tight through arena. stride 0
// means tightly packed. Shared by every image upload path (atlas, large static, dynamic).
void tex_sub_image_strided(FlArena* arena, u32 x, u32 y, u32 w, u32 h, u32 stride, u32 bpp, GLenum format,
                           const u8* pixels) {
    u32 tight = w * bpp;
    if (stride == 0) {
        stride = tight;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (stride != tight) {
        if (stride % bpp == 0) {
            glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)(stride / bpp));
        } else {
            u8* repacked = arena_alloc_array(arena, u8, (size_t)tight * h);
            for (u32 row = 0; row < h; row++) {
                memcpy(repacked + (size_t)row * tight, pixels + (size_t)row * stride, tight);
            }
            pixels = repacked;
        }
    }
    glTexSubImage2D(GL_TEXTURE_2D, 0, (GLint)x, (GLint)y, (GLsizei)w, (GLsizei)h, format, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

// Realize and bind this frame's slot assignments: create missing dynamic textures, stream dirty dynamic
// content, bind each slot's texture + filter sampler to its unit, and hand the raster pass the reciprocal
// extents. Must run with raster_prog current (the uniform upload) and before the raster dispatch.
static void bind_frame_slot_textures(GpuShared* self) {
    const GpuGl* gl = &self->gl;
    GpuImageSlots* slots = &self->cpu.image_slots;
    f32 inv_size[GPU_IMAGE_SLOT_COUNT * 2];
    for (u32 i = 0; i < GPU_IMAGE_SLOT_COUNT * 2; i++) {
        inv_size[i] = 1.0f; // unassigned slots are never sampled; keep the divisor defined anyway
    }
    for (u32 s = 0; s < slots->slot_count; s++) {
        GpuSlotImage* img = &slots->images[slots->slot_image[s]];
        glActiveTexture(GL_TEXTURE0 + GPU_IMAGE_SLOT_UNIT0 + (GLenum)s);
        if (img->tex == 0) {
            // A dynamic entry awaiting realization - statics arrive realized from prepare_image.
            img->tex = (u32)create_slot_texture(gl, img->w, img->h, img->alpha_only, true);
        } else {
            glBindTexture(GL_TEXTURE_2D, (GLuint)img->tex);
        }
        if (img->dirty) {
            // Stream the referencing frame's content. The frame arena backs any repack and is rewound
            // only after the dispatch, so the copy lives long enough.
            tex_sub_image_strided(self->frame_arena, 0, 0, img->w, img->h, img->stride, img->alpha_only ? 1u : 4u,
                                  img->alpha_only ? GL_RED : GL_RGBA, img->pixels);
            img->dirty = false;
            self->cpu.dynamic_upload_count++; // one tick per dynamic stream, only on referencing frames
        }
        gl->BindSampler(GPU_IMAGE_SLOT_UNIT0 + s,
                        slots->slot_filter[s] != 0 ? self->sampler_nearest : self->sampler_linear);
        inv_size[s * 2 + 0] = 1.0f / (f32)img->w;
        inv_size[s * 2 + 1] = 1.0f / (f32)img->h;
    }
    glActiveTexture(GL_TEXTURE0);
    gl->Uniform2fv(self->u_raster_slot_inv_size, GPU_IMAGE_SLOT_COUNT, inv_size);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shared-orchestrator ops: the GL half of the GpuRenderOps table, driven by gpu_orchestrate_frame in the
// canonical per-frame order. The backend/surface handles arrive as void* and cast back to GpuShared/GpuSurface.
// GL binds directly to the current context, so begin_encoder is a no-op and the pass ordering rides GL memory
// barriers.

// Per-frame preamble: reset the glyph-dispatch counter (refilled by the prepare pass), then
// advance the image-slot table (evicting stale dynamic entries) and delete textures freed since the last render -
// both before translation, which rebuilds the slot table.
static void gl_op_frame_begin(void* backend) {
    GpuShared* self = backend;
    self->glyph_dispatch_count = 0;
    self->viewport3d_frame++;
    gpu_image_slots_begin_frame(&self->cpu.image_slots);
    delete_dead_slot_textures(self);
}

static struct FlFontCache* gl_op_get_font_cache(void* backend) {
    return ((GpuShared*)backend)->font_cache;
}

// Translate the neutral IR into the backend's draw-command + AABB + clip arrays. The frame envelope's
// background state translates to clear + fullscreen image commands at the front of the stream.
static GpuDrawList gl_op_translate(void* backend, const struct FlRenderFrame* frame, u32 screen_w, u32 screen_h) {
    GpuShared* self = backend;
    GpuDrawList list
        = gpu_ir_to_draw_cmds(self->frame_arena, frame->commands, frame->bytes, frame->global_alpha, screen_w, screen_h,
                              &self->cpu.image_atlas, &self->cpu.glyph_atlas, &self->cpu.image_slots,
                              &frame->background, &self->cpu.svg_atlas, &self->cpu.color_svg_atlas);
    gpu_prepare_log_slot_overflow(&self->cpu.image_slots);
    return list;
}

// GL renders into the current context with no explicit encoder, so opening one is a no-op.
static void gl_op_begin_encoder(void* backend, void* surface) {
    (void)backend;
    (void)surface;
}

// Upload the draw arrays into the shared input SSBOs and bind them to their base points. An empty frame still runs
// both passes - classification zeroes every tile count and raster clears the target to transparent - so on such a
// frame the SSBOs just keep their previous storage. The per-surface tile SSBO (base 2, classify's output) is
// bound in the classify op, which has the surface handle.
static void gl_op_upload_inputs(void* backend, const GpuDrawList* list) {
    GpuShared* self = backend;
    const GpuGl* gl = &self->gl;
    if (list->count > 0) {
        gl->BindBuffer(GL_SHADER_STORAGE_BUFFER, self->cmd_ssbo);
        gl->BufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)(list->count * sizeof(GpuDrawCmd)), list->cmds,
                       GL_STREAM_DRAW);
        gl->BindBuffer(GL_SHADER_STORAGE_BUFFER, self->aabb_ssbo);
        gl->BufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)(list->count * sizeof(GpuAabb)), list->aabbs,
                       GL_STREAM_DRAW);
        gl->BindBuffer(GL_SHADER_STORAGE_BUFFER, self->clip_ssbo);
        gl->BufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)(list->clip_count * sizeof(GpuAabb)), list->clips,
                       GL_STREAM_DRAW);
    }
    gl->BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, self->cmd_ssbo);
    gl->BindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, self->aabb_ssbo);
    gl->BindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, self->clip_ssbo);
}

// Pass 1: classification - one thread per tile, GPU_TILE_SIZE-square groups. Binds the per-surface tile-list SSBO
// (base 2, this pass's output, read back by raster) before dispatch.
static void gl_op_classify(void* backend, void* surface, u32 cmd_count) {
    GpuShared* self = backend;
    GpuSurface* surf = surface;
    const GpuGl* gl = &self->gl;
    gl->BindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, surf->tile_ssbo);
    gl->UseProgram(self->classify_prog);
    gl->Uniform1ui(self->u_classify_cmd_count, cmd_count);
    gl->Uniform1ui(self->u_classify_tile_cols, (u32)surf->tile_cols);
    gl->Uniform1ui(self->u_classify_tile_rows, (u32)surf->tile_rows);
    gl->DispatchCompute(((u32)surf->tile_cols + GPU_TILE_SIZE - 1u) / GPU_TILE_SIZE,
                        ((u32)surf->tile_rows + GPU_TILE_SIZE - 1u) / GPU_TILE_SIZE, 1);
}

// Tile lists written by pass 1 are read as SSBOs by pass 2.
static void gl_op_barrier(void* backend) {
    GpuShared* self = backend;
    self->gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

// Pass 2: raster - one workgroup per tile. The atlas rides on units 1 (linear) and 2 (nearest), the frame's slot
// textures on units GPU_IMAGE_SLOT_UNIT0+; image uploads happen before this dispatch on this thread, so no
// barrier is needed for them.
static void gl_op_raster(void* backend, void* surface) {
    GpuShared* self = backend;
    GpuSurface* surf = surface;
    const GpuGl* gl = &self->gl;
    gl->UseProgram(self->raster_prog);
    gl->Uniform1ui(self->u_raster_tile_cols, (u32)surf->tile_cols);
    gl->Uniform2ui(self->u_raster_screen_size, (u32)surf->screen_width, (u32)surf->screen_height);
    gl->Uniform1ui(self->u_raster_debug_mode, (u32)self->debug_mode);
    gl->BindImageTexture(0, surf->target_tex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, self->atlas_tex);
    gl->BindSampler(1, self->sampler_linear);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, self->atlas_tex);
    gl->BindSampler(2, self->sampler_nearest);
    glActiveTexture(GL_TEXTURE0);
    bind_frame_slot_textures(self);
    // Glyph coverage atlas on unit 0 (sampled by KIND_GLYPH commands), linear-filtered like an image.
    // bind_frame_slot_textures returns with unit 0 active, so no glActiveTexture is needed here.
    glBindTexture(GL_TEXTURE_2D, self->glyph_atlas_tex);
    gl->BindSampler(0, self->sampler_linear);
    // SVG coverage atlas on its dedicated unit (sampled by KIND_SVG commands), linear-filtered like the glyph
    // atlas.
    glActiveTexture(GL_TEXTURE0 + GPU_SVG_ATLAS_UNIT);
    glBindTexture(GL_TEXTURE_2D, self->svg_atlas_tex);
    gl->BindSampler(GPU_SVG_ATLAS_UNIT, self->sampler_linear);
    // RGBA8 multi-color SVG atlas on the next unit (sampled by KIND_SVG_COLOR commands as a premultiplied
    // image source), linear-filtered like the coverage atlases.
    glActiveTexture(GL_TEXTURE0 + GPU_COLOR_SVG_ATLAS_UNIT);
    glBindTexture(GL_TEXTURE_2D, self->color_svg_atlas_tex);
    gl->BindSampler(GPU_COLOR_SVG_ATLAS_UNIT, self->sampler_linear);
    glActiveTexture(GL_TEXTURE0);
    gl->DispatchCompute((u32)surf->tile_cols, (u32)surf->tile_rows, 1);
}

// The target is consumed via framebuffer reads (readback now, the host's composite later) and texture fetches,
// never as a further image store, so fence those two access classes.
static void gl_op_end_encoder(void* backend) {
    GpuShared* self = backend;
    self->gl.MemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
}

static const GpuRenderOps gl_render_ops = {
    .frame_begin = gl_op_frame_begin,
    .get_font_cache = gl_op_get_font_cache,
    .prepare_glyph = prepare_glyph,
    .translate = gl_op_translate,
    .begin_encoder = gl_op_begin_encoder,
    .upload_inputs = gl_op_upload_inputs,
    .classify = gl_op_classify,
    .barrier = gl_op_barrier,
    .raster = gl_op_raster,
    .end_encoder = gl_op_end_encoder,
};

// Render one frame: the two-dispatch all-compute pipeline, driven by the shared orchestrator
// (gpu_orchestrator.c) through the GL ops above.
static void render(void* user_data, FlSurface* surface_handle, const FlRenderFrame* frame) {
    perf_scope("gpu_render");
    GpuShared* self = user_data;
    GpuSurface* surface = (GpuSurface*)surface_handle;
    gpu_orchestrate_frame(&gl_render_ops, self, surface, frame, (u32)surface->screen_width, (u32)surface->screen_height,
                          &self->cpu.glyph_atlas, self->frame_arena, self->perm_arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Bind the shared font cache so render() can pull glyph outlines and realize them into the atlas itself.
static void set_font_cache(void* user_data, struct FlFontCache* font_cache) {
    GpuShared* self = user_data;
    self->font_cache = font_cache;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Optional pixel-readback capability: explicit GPU -> CPU readback of the offscreen target as interleaved
// RGBA8 (premultiplied coverage alpha). Row 0 is the top UI row - the imageStore pixel grid and glReadPixels
// both address texel row 0 first, so no flip is needed. Off the per-frame present path.

static bool read_pixels(void* user_data, FlSurface* surface_handle, FlPixelReadback* out) {
    GpuShared* self = user_data;
    GpuSurface* surface = (GpuSurface*)surface_handle;
    const GpuGl* gl = &self->gl;

    gl->BindFramebuffer(GL_FRAMEBUFFER, surface->readback_fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, surface->screen_width, surface->screen_height, GL_RGBA, GL_UNSIGNED_BYTE, surface->readback_buf);
    gl->BindFramebuffer(GL_FRAMEBUFFER, 0);

    *out = (FlPixelReadback) {
        .pixels = surface->readback_buf,
        .width = (u32)surface->screen_width,
        .height = (u32)surface->screen_height,
        .stride = (u32)surface->screen_width * 4u,
    };
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Surface-texture seam (gpu_renderer.h): the offscreen target's GL texture name + current
// dimensions, for a host to sample and composite. The texture name crosses as a plain u32 - no GL type here.
// Per-surface: the host passes the surface handle it rendered into, one per window.

GpuSurfaceTexture gpu_renderer_surface_texture(FlSurface* surface_handle) {
    GpuSurface* surface = (GpuSurface*)surface_handle;
    return (GpuSurfaceTexture) {
        .handle = (u32)surface->target_tex,
        .width = surface->screen_width,
        .height = surface->screen_height,
    };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void destroy(void* user_data) {
    GpuShared* self = user_data;
    const GpuGl* gl = &self->gl;

    // Before the slot sweep below: this releases the viewports' framebuffers and depth buffers but leaves
    // their color textures in the slot table, which that sweep then deletes along with every other one.
    gpu_viewport3d_destroy(self);

    delete_dead_slot_textures(self);
    for (u32 i = 0; i < self->cpu.image_slots.image_count; i++) {
        if (self->cpu.image_slots.images[i].tex != 0) {
            GLuint tex = (GLuint)self->cpu.image_slots.images[i].tex;
            glDeleteTextures(1, &tex);
        }
    }

    // Shared SSBOs only - the per-tile classification SSBO is per-surface (freed in destroy_surface). The host
    // tears down every surface before the backend, so no surface GL objects survive to here.
    GLuint buffers[5] = { self->cmd_ssbo, self->aabb_ssbo, self->clip_ssbo, self->curve_ssbo, self->shapes_ssbo };
    gl->DeleteBuffers(5, buffers);
    glDeleteTextures(1, &self->atlas_tex);
    glDeleteTextures(1, &self->glyph_atlas_tex);
    glDeleteTextures(1, &self->svg_atlas_tex);
    glDeleteTextures(1, &self->color_svg_atlas_tex);
    GLuint samplers[2] = { self->sampler_linear, self->sampler_nearest };
    gl->DeleteSamplers(2, samplers);
    gl->DeleteProgram(self->classify_prog);
    gl->DeleteProgram(self->raster_prog);
    gl->DeleteProgram(self->glyph_prog);
    gl->DeleteProgram(self->color_svg_prog);

    fl_arena_destroy(self->frame_arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlRenderBackend create_gpu_renderer(struct FlArena* arena) {
    GpuShared* self = arena_alloc_zero(arena, GpuShared);
    self->perm_arena = arena;

    if (!gpu_gl_load(&self->gl)) {
        return (FlRenderBackend) { 0 };
    }

    GLint major = 0;
    GLint minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    if (major * 10 + minor < 43) {
        log_error("gpu backend: GL %d.%d below the 4.3 core floor", major, minor);
        return (FlRenderBackend) { 0 };
    }

    const GpuGl* gl = &self->gl;
    const char* srcs[GPU_SHADER_MAX_SOURCES];
    self->classify_prog = compile_program(gl, srcs, gpu_classify_sources(srcs), "classification shader");
    self->raster_prog = compile_program(gl, srcs, gpu_raster_sources(srcs), "raster shader");
    self->glyph_prog = compile_program(gl, srcs, gpu_glyph_sources(srcs), "glyph raster shader");
    self->color_svg_prog = compile_program(gl, srcs, gpu_color_svg_sources(srcs), "color svg raster shader");
    if (self->classify_prog == 0 || self->raster_prog == 0 || self->glyph_prog == 0 || self->color_svg_prog == 0) {
        // glDeleteProgram ignores 0, so deleting all four releases exactly the programs that compiled.
        gl->DeleteProgram(self->classify_prog);
        gl->DeleteProgram(self->raster_prog);
        gl->DeleteProgram(self->glyph_prog);
        gl->DeleteProgram(self->color_svg_prog);
        return (FlRenderBackend) { 0 };
    }

    self->u_classify_cmd_count = gl->GetUniformLocation(self->classify_prog, "cmd_count");
    self->u_classify_tile_cols = gl->GetUniformLocation(self->classify_prog, "tile_cols");
    self->u_classify_tile_rows = gl->GetUniformLocation(self->classify_prog, "tile_rows");
    self->u_raster_tile_cols = gl->GetUniformLocation(self->raster_prog, "tile_cols");
    self->u_raster_screen_size = gl->GetUniformLocation(self->raster_prog, "screen_size");
    self->u_raster_debug_mode = gl->GetUniformLocation(self->raster_prog, "debug_mode");
    self->u_glyph_cell_origin = gl->GetUniformLocation(self->glyph_prog, "cell_origin");
    self->u_glyph_cell_size = gl->GetUniformLocation(self->glyph_prog, "cell_size");
    self->u_glyph_seg_count = gl->GetUniformLocation(self->glyph_prog, "seg_count");
    self->u_color_svg_cell_origin = gl->GetUniformLocation(self->color_svg_prog, "cell_origin");
    self->u_color_svg_cell_size = gl->GetUniformLocation(self->color_svg_prog, "cell_size");
    self->u_color_svg_shape_count = gl->GetUniformLocation(self->color_svg_prog, "shape_count");

    // The atlas samplers and the slot sampler array live on fixed texture units; set once (the program
    // object keeps them). Per-frame slot state is the textures bound to those units + slot_inv_size.
    gl->UseProgram(self->raster_prog);
    gl->Uniform1i(gl->GetUniformLocation(self->raster_prog, "atlas_linear"), 1);
    gl->Uniform1i(gl->GetUniformLocation(self->raster_prog, "atlas_nearest"), 2);
    // The glyph atlas samples on texture unit 0 - the upload scratch unit, free of a sampler during the raster
    // dispatch. Its texture is bound there per frame below.
    gl->Uniform1i(gl->GetUniformLocation(self->raster_prog, "glyph_atlas"), 0);
    // The SVG coverage atlas samples on the unit just past the image slots, bound per frame below.
    gl->Uniform1i(gl->GetUniformLocation(self->raster_prog, "svg_atlas"), GPU_SVG_ATLAS_UNIT);
    // The RGBA8 multi-color SVG atlas samples on the next unit, bound per frame below.
    gl->Uniform1i(gl->GetUniformLocation(self->raster_prog, "svg_color_atlas"), GPU_COLOR_SVG_ATLAS_UNIT);
    GLint slot_units[GPU_IMAGE_SLOT_COUNT];
    for (int i = 0; i < GPU_IMAGE_SLOT_COUNT; i++) {
        slot_units[i] = GPU_IMAGE_SLOT_UNIT0 + i;
    }
    gl->Uniform1iv(gl->GetUniformLocation(self->raster_prog, "slot_tex"), GPU_IMAGE_SLOT_COUNT, slot_units);
    self->u_raster_slot_inv_size = gl->GetUniformLocation(self->raster_prog, "slot_inv_size");
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &self->max_texture_size);

    self->frame_arena = arena_new();

    // Shared per-frame SSBOs (one frame in flight). The per-tile classification SSBO is per-surface and genned
    // in create_surface; the readback buffer's arena is per-surface too.
    GLuint buffers[5];
    gl->GenBuffers(5, buffers);
    self->cmd_ssbo = buffers[0];
    self->aabb_ssbo = buffers[1];
    self->clip_ssbo = buffers[2];
    self->curve_ssbo = buffers[3];
    self->shapes_ssbo = buffers[4];

    GpuDrawCmd zero_cmd = { 0 };
    GpuAabb zero_aabb = { 0 };
    seed_empty_ssbo(gl, self->cmd_ssbo, sizeof(zero_cmd), &zero_cmd);
    seed_empty_ssbo(gl, self->aabb_ssbo, sizeof(zero_aabb), &zero_aabb);
    seed_empty_ssbo(gl, self->clip_ssbo, sizeof(zero_aabb), &zero_aabb);

    // Image atlas: CPU bookkeeping + the RGBA8 texture. Immutable storage, one level (no mips - explicit-LOD
    // sampling). The sampler-slot bookkeeping for large/dynamic images sits alongside; its
    // textures are created on demand (prepare seam / render).
    gpu_image_atlas_init(&self->cpu.image_atlas, arena);
    gpu_image_slots_init(&self->cpu.image_slots, arena);
    glGenTextures(1, &self->atlas_tex);
    glBindTexture(GL_TEXTURE_2D, self->atlas_tex);
    gl->TexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, GPU_IMAGE_ATLAS_SIZE, GPU_IMAGE_ATLAS_SIZE);

    // Glyph atlas: R8 coverage, image-stored by the glyph-raster dispatch. Never cleared - a
    // prepared cell is fully written including its padding ring, and nothing samples outside prepared cells.
    gpu_glyph_atlas_init(&self->cpu.glyph_atlas, arena);
    glGenTextures(1, &self->glyph_atlas_tex);
    glBindTexture(GL_TEXTURE_2D, self->glyph_atlas_tex);
    gl->TexStorage2D(GL_TEXTURE_2D, 1, GL_R8, GPU_GLYPH_ATLAS_SIZE, GPU_GLYPH_ATLAS_SIZE);

    // SVG vector-icon atlas: R8 coverage, image-stored by the glyph-raster dispatch reused for monochrome
    // icons. Never cleared - every prepared cell including its padding ring is fully written, and nothing
    // samples outside a prepared cell.
    gpu_svg_atlas_init(&self->cpu.svg_atlas, arena, GpuSvgTexel_R8);
    glGenTextures(1, &self->svg_atlas_tex);
    glBindTexture(GL_TEXTURE_2D, self->svg_atlas_tex);
    gl->TexStorage2D(GL_TEXTURE_2D, 1, GL_R8, GPU_SVG_ATLAS_SIZE_MONO, GPU_SVG_ATLAS_SIZE_MONO);

    // Multi-color SVG atlas: the same bookkeeping as the R8 atlas, but RGBA8 - its cells hold baked
    // premultiplied color composited by the color-raster dispatch. Never cleared either; every prepared cell
    // including its padding ring is fully written (the ring stays transparent).
    gpu_svg_atlas_init(&self->cpu.color_svg_atlas, arena, GpuSvgTexel_Rgba8);
    glGenTextures(1, &self->color_svg_atlas_tex);
    glBindTexture(GL_TEXTURE_2D, self->color_svg_atlas_tex);
    gl->TexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, GPU_SVG_ATLAS_SIZE_COLOR, GPU_SVG_ATLAS_SIZE_COLOR);

    self->sampler_linear = create_sampler(gl, GL_LINEAR);
    self->sampler_nearest = create_sampler(gl, GL_NEAREST);

    // No surface is created here - the host builds per-window targets via the surface seam, so the
    // resolution-dependent resources are created in gpu_create_surface.

    // The core's default scale.
    self->last_ui_scale = 1.0f;

    // The 3D tier is optional: if its program fails to build, the seam below stays unbound and the core draws
    // a placeholder for every viewport rather than the backend failing to construct.
    bool has_3d = gpu_viewport3d_create(self);

    return (FlRenderBackend) {
        .user_data = self,
        .create_surface = gpu_create_surface,
        .destroy_surface = gpu_destroy_surface,
        .change_resolution = change_resolution,
        .render = render,
        .destroy = destroy,
        .scale_changed = scale_changed,
        // render() realizes the frame's glyphs into the atlas from this cache. A host driving prepare_glyph
        // itself may leave it unbound; the core null-checks the seam.
        .set_font_cache = set_font_cache,
        .prepare_image = prepare_image,
        .release_image = gpu_release_image,
        .prepare_glyph = prepare_glyph,
        .prepare_vector = prepare_vector,
        .read_pixels = read_pixels,
        // The 3D seam, bound as a group or not at all.
        .mesh_create = has_3d ? gpu_mesh_create : nullptr,
        .mesh_destroy = has_3d ? gpu_mesh_destroy : nullptr,
        .viewport3d_submit = has_3d ? gpu_viewport3d_submit : nullptr,
        // Realization is GL-thread-confined: decode results queue for the begin-frame pump on the
        // main/render thread instead of running in the job worker.
        .prepare_image_on_job = false,
    };
}
