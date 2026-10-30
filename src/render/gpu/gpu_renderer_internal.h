#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GPU backend internal seam: the GpuShared / GpuSurface state objects and the helpers shared across the backend's
// translation units. The Metal build cannot include this header - it pulls in GL types.

#include "gpu_debug.h"
#include "gpu_gl.h"
#include "gpu_cpu_state.h"
#include <stddef.h>
#include "gpu_glyph_atlas.h"
#include "gpu_image_atlas.h"
#include "gpu_image_slots.h"
#include "gpu_renderer.h"
#include "gpu_svg_atlas.h"
#include <core/arena.h>
#include <ui/render_ir.h>

struct FlFontCache;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Per-surface tier: the offscreen RGBA8 tile-raster target, its readback FBO, the per-tile classification SSBO
// sized to this surface's tile grid, and the CPU readback buffer. One arena backs both this struct and the
// readback buffer; setup_resolution rewinds to readback_reset to reallocate the buffer, leaving the struct intact.
typedef struct GpuSurface {
    FlArena* arena; // per-surface; backs this struct + the readback buffer, destroyed in destroy_surface
    FlTempArena
        readback_reset; // savepoint past this struct; setup_resolution rewinds here before (re)allocating readback_buf

    int screen_width;
    int screen_height;
    int tile_cols;
    int tile_rows;

    GLuint tile_ssbo;    // per-tile classification list, sized to this surface's tile grid
    GLuint target_tex;   // offscreen RGBA8 render target
    GLuint readback_fbo; // FBO wrapping target_tex for glReadPixels
    void* readback_buf;  // CPU readback destination (arena-backed, past readback_reset)
} GpuSurface;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 3D tier (gpu_viewport3d.c): retained meshes and per-viewport color+depth targets, both backend-shared. A
// viewport is keyed by the element id the core hands down. Fixed caps; both overflow with a diagnostic.

#define GPU_MESH_CAP 128
#define GPU_VIEWPORT3D_CAP 8

typedef struct GpuMesh {
    GLuint vao; // 0 marks a free table slot
    GLuint vbo;
    GLuint ibo;
    u32 index_count;
    GLenum primitive; // GL_TRIANGLES or GL_LINES, fixed at creation by the mesh's topology
} GpuMesh;

typedef struct GpuViewport3d {
    u32 id;         // the core's element id for this viewport
    u64 last_frame; // GpuShared.viewport3d_frame at the viewport's latest submit
    GLuint fbo;
    GLuint depth_rb;
    FlImageHandle color; // the color attachment's image handle; the texture itself lives in the slot table
    u32 width;
    u32 height;
} GpuViewport3d;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct GpuShared {
    GpuCpuState cpu;      // first member: the backend user_data doubles as the GpuCpuState the shared seams read
    FlArena* perm_arena;  // the create-time arena, kept for wholesale glyph-atlas resets
    FlArena* frame_arena; // per-frame IR translation scratch, rewound each render
    GpuGl gl;

    // Last UI scale handed to the scale_changed seam. A change repacks the glyph/SVG atlases from the origin.
    float last_ui_scale;

    GLuint classify_prog;
    GLuint raster_prog;
    GLuint cmd_ssbo;
    GLuint aabb_ssbo;
    GLuint clip_ssbo;

    // Image atlas: CPU bookkeeping (handle -> cell) + the RGBA8 texture it indexes into, sampled by the
    // raster pass through two sampler objects (one per IR filter mode) on texture units 1 and 2.
    GLuint atlas_tex;
    GLuint sampler_linear;
    GLuint sampler_nearest;

    // Sampler-slot images (gpu_image_slots.h): large statics on dedicated textures, dynamic buffers on
    // streaming textures - bound per frame to units GPU_IMAGE_SLOT_UNIT0..+GPU_IMAGE_SLOT_COUNT-1.
    GLint max_texture_size;

    // The shared font layer (bound via set_font_cache). render() pulls each text run's glyph outlines from it to
    // realize them into the atlas below. NULL when a host drives prepare_glyph itself.
    struct FlFontCache* font_cache;

    // Glyph atlas: CPU bookkeeping (key -> cell + metrics) + the R8 texture the glyph-raster
    // dispatch image-stores coverage into, fed per glyph through the curve SSBO.
    GLuint glyph_atlas_tex;
    GLuint glyph_prog;
    GLuint curve_ssbo;

    // SVG vector-icon atlas: CPU bookkeeping (handle -> cell + authored fill) + an R8 coverage texture. Monochrome
    // icons rasterize into it through the glyph-raster dispatch (glyph_prog + curve_ssbo), and draw via the SVG
    // alpha-coverage path sampling it on GPU_SVG_ATLAS_UNIT.
    GLuint svg_atlas_tex;

    // Multi-color SVG atlas: a second GpuSvgAtlas instance in RGBA8. Multi-color icons composite their shapes in
    // document order into a cell here via color_svg_prog (per-shape records ride shapes_ssbo, the quads share
    // curve_ssbo with the mono path), and draw via the image path sampling it on GPU_COLOR_SVG_ATLAS_UNIT.
    GLuint color_svg_atlas_tex;
    GLuint color_svg_prog;
    GLuint shapes_ssbo;

    u64 viewport3d_frame;     // advanced once per rendered frame; stamps viewport submits so idle ones can be retired
    u32 glyph_dispatch_count; // glyph cell-raster dispatches this frame. render() zeroes it before the
                              // prepare_text_glyphs pass; prepare_glyph ticks it once per DispatchCompute, so a
                              // resident short-circuit does not count.

    // 3D tier: the unlit vertex/fragment program plus the retained meshes and per-viewport targets it draws with.
    // mesh3d_prog is 0 when the program failed to build, which leaves the 3D seam unbound.
    GLuint mesh3d_prog;
    GpuMesh meshes[GPU_MESH_CAP];
    GpuViewport3d viewport3d[GPU_VIEWPORT3D_CAP];
    u32 viewport3d_count;

    GpuDebugMode debug_mode;

    // Uniform locations, resolved once at link.
    GLint u_classify_cmd_count;
    GLint u_classify_tile_cols;
    GLint u_classify_tile_rows;
    GLint u_raster_tile_cols;
    GLint u_raster_screen_size;
    GLint u_raster_debug_mode;
    GLint u_raster_slot_inv_size;
    GLint u_glyph_cell_origin;
    GLint u_glyph_cell_size;
    GLint u_glyph_seg_count;
    GLint u_color_svg_cell_origin;
    GLint u_color_svg_cell_size;
    GLint u_color_svg_shape_count;
    GLint u_mesh3d_view_projection;
    GLint u_mesh3d_has_texture;
} GpuShared;
_Static_assert(offsetof(GpuShared, cpu) == 0, "cpu must be the first member");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shared GL upload helpers.

// Create a slot-tier texture and leave it bound on the active unit. Alpha-coverage sources are R8 swizzled to
// (r, r, r, r); color sources are RGBA8, force_opaque swizzling alpha to 1 for dynamic color buffers.
GLuint create_slot_texture(const GpuGl* gl, u32 w, u32 h, bool alpha_only, bool force_opaque);

// Upload possibly-padded rows into the texture bound on the active unit. stride 0 means tightly packed; the
// rare non-pixel-multiple stride repacks tight through arena.
void tex_sub_image_strided(FlArena* arena, u32 x, u32 y, u32 w, u32 h, u32 stride, u32 bpp, GLenum format,
                           const u8* pixels);

// Compile one shader stage from an assembled source array. Returns the shader name, or 0 (logged, with the
// driver's info log) on failure.
GLuint gpu_compile_shader(const GpuGl* gl, GLenum stage, const char* const* sources, int count, const char* name);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 3D seams (gpu_viewport3d.c). The last three are wired into the FlRenderBackend vtable only when create succeeded.

bool gpu_viewport3d_create(GpuShared* self);
void gpu_viewport3d_destroy(GpuShared* self);

FlMeshHandle gpu_mesh_create(void* user_data, const FlMeshVertex* vertices, u32 vertex_count, const u16* indices,
                             u32 index_count, FlMeshTopology topology);
void gpu_mesh_destroy(void* user_data, FlMeshHandle handle);
void gpu_viewport3d_submit(void* user_data, u32 viewport_id, FlImageHandle color, u32 width, u32 height,
                           const f32* view_projection, const FlViewport3dDraw* draws, u32 draw_count);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Resource-prepare seams (gpu_resource_prepare.c): image/glyph/SVG realization.

void prepare_image(void* user_data, FlImageLoadRequestId request_id, const FlImageDecodeResult* decoded,
                   FlArena* scratch, const FlImageLoadRequest* request);
void prepare_glyph(void* user_data, FlFontId font, u16 size, u32 glyph_id, const struct FlGlyphOutline* outline);
void prepare_vector(void* user_data, FlImageHandle handle, u16 size, const FlVectorImage* vector);
