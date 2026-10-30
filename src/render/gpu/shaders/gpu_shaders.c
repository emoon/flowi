#include "../gpu_shaders.h"
#include "../gpu_bindings.h"
#include "../gpu_debug.h"
#include "../gpu_glyph_atlas.h"
#include "../gpu_image_atlas.h"
#include "../gpu_image_slots.h"
#include "../gpu_svg_atlas.h"

#include <core/log.h>
#include <stdio.h>

// The shader snippets and bodies, embedded from src/render/gpu/shaders/*.glsl as NUL-terminated byte
// arrays by the build (cmake/embed_glsl.cmake). The shared-vocabulary compat header is embedded the
// same way and spliced ahead of every body: shader_compat_glsl.h on GL, shader_compat_metal.h on Metal
// (selected by FLOWI_SHADER_BACKEND_METAL, the compile define the APPLE build sets - the same corpus of *.glsl
// bodies compiles as GLSL or MSL depending only on which compat header + dialect preamble is spliced).
#ifdef FLOWI_SHADER_BACKEND_METAL
extern const char gpu_glsl_shader_compat_metal[];
#define gpu_glsl_shader_compat gpu_glsl_shader_compat_metal
#else
extern const char gpu_glsl_shader_compat_glsl[];
#define gpu_glsl_shader_compat gpu_glsl_shader_compat_glsl
#endif
extern const char gpu_glsl_tile_defs[];
extern const char gpu_glsl_kind_defs[];
extern const char gpu_glsl_drawcmd[];
extern const char gpu_glsl_segment[];
extern const char gpu_glsl_coverage_kernel[];
extern const char gpu_glsl_classify[];
extern const char gpu_glsl_raster[];
extern const char gpu_glsl_glyph[];
extern const char gpu_glsl_color_svg[];
extern const char gpu_glsl_mesh3d_vert[];
extern const char gpu_glsl_mesh3d_frag[];

// The per-backend dialect preamble: the one source line naming the shading language the bodies
// compile as. Kept separate from the generated #define preamble below (host constants), which both dialects
// consume unchanged.
static const char* gpu_shader_preamble(void) {
#ifdef FLOWI_SHADER_BACKEND_METAL
    // Metal dialect: pull in the standard library and open the metal namespace, so the bodies' texture2d /
    // as_type / unpack_unorm4x8_to_float / threadgroup_barrier references resolve. No offline .metallib step -
    // the source compiles at renderer init via newLibraryWithSource:.
    return "#include <metal_stdlib>\nusing namespace metal;\n";
#else
    return "#version 430\n";
#endif
}

// The generated #define preamble: the host's constants (tile geometry, atlas sizes, slot count) plus the whole
// resource-binding contract (gpu_bindings.h) emitted as #defines so the shaders reference them by name and share
// one definition with the C side. Built once into a static buffer.
static const char* gpu_shader_defines(void) {
    static char buf[2048];
    static int built = 0;
    if (!built) {
        int written = snprintf(
            buf, sizeof buf,
            "#define TILE_SIZE %d\n"
            "#define TILE_CAPACITY %du\n"
            "#define IMAGE_SLOT_COUNT %d\n"
            "#define IMAGE_ATLAS_SIZE %d\n"
            "#define GLYPH_ATLAS_SIZE %d\n"
            "#define SVG_ATLAS_SIZE %d\n"
            "#define SVG_COLOR_ATLAS_SIZE %d\n"
            "#define BIND_CLASSIFY_CMDS %d\n"
            "#define BIND_CLASSIFY_AABBS %d\n"
            "#define BIND_CLASSIFY_TILE_LISTS %d\n"
            "#define BIND_CLASSIFY_CMD_COUNT %d\n"
            "#define BIND_CLASSIFY_TILE_COLS %d\n"
            "#define BIND_CLASSIFY_TILE_ROWS %d\n"
            "#define BIND_RASTER_CMDS %d\n"
            "#define BIND_RASTER_TILE_LISTS %d\n"
            "#define BIND_RASTER_CLIPS %d\n"
            "#define BIND_RASTER_TILE_COLS %d\n"
            "#define BIND_RASTER_SCREEN_SIZE %d\n"
            "#define BIND_RASTER_DEBUG_MODE %d\n"
            "#define BIND_RASTER_SLOT_INV_SIZE %d\n"
            "#define BIND_RASTER_SLOT_FILTER %d\n"
            "#define BIND_RASTER_TEX_TARGET %d\n"
            "#define BIND_RASTER_TEX_GLYPH %d\n"
            "#define BIND_RASTER_TEX_IMAGE %d\n"
            "#define BIND_RASTER_TEX_SVG %d\n"
            "#define BIND_RASTER_TEX_SVG_COLOR %d\n"
            "#define BIND_RASTER_TEX_SLOT0 %d\n"
            "#define BIND_GLYPH_CURVES %d\n"
            "#define BIND_GLYPH_CELL_ORIGIN %d\n"
            "#define BIND_GLYPH_CELL_SIZE %d\n"
            "#define BIND_GLYPH_SEG_COUNT %d\n"
            "#define BIND_GLYPH_TEX_ATLAS %d\n"
            "#define BIND_COLOR_SVG_CURVES %d\n"
            "#define BIND_COLOR_SVG_SHAPES %d\n"
            "#define BIND_COLOR_SVG_CELL_ORIGIN %d\n"
            "#define BIND_COLOR_SVG_CELL_SIZE %d\n"
            "#define BIND_COLOR_SVG_SHAPE_COUNT %d\n"
            "#define BIND_COLOR_SVG_TEX_ATLAS %d\n",
            GPU_TILE_SIZE, GPU_TILE_CAPACITY, GPU_IMAGE_SLOT_COUNT, GPU_IMAGE_ATLAS_SIZE, GPU_GLYPH_ATLAS_SIZE,
            GPU_SVG_ATLAS_SIZE_MONO, GPU_SVG_ATLAS_SIZE_COLOR, GPU_BIND_CLASSIFY_CMDS, GPU_BIND_CLASSIFY_AABBS,
            GPU_BIND_CLASSIFY_TILE_LISTS, GPU_BIND_CLASSIFY_CMD_COUNT, GPU_BIND_CLASSIFY_TILE_COLS,
            GPU_BIND_CLASSIFY_TILE_ROWS, GPU_BIND_RASTER_CMDS, GPU_BIND_RASTER_TILE_LISTS, GPU_BIND_RASTER_CLIPS,
            GPU_BIND_RASTER_TILE_COLS, GPU_BIND_RASTER_SCREEN_SIZE, GPU_BIND_RASTER_DEBUG_MODE,
            GPU_BIND_RASTER_SLOT_INV_SIZE, GPU_BIND_RASTER_SLOT_FILTER, GPU_BIND_RASTER_TEX_TARGET,
            GPU_BIND_RASTER_TEX_GLYPH, GPU_BIND_RASTER_TEX_IMAGE, GPU_BIND_RASTER_TEX_SVG,
            GPU_BIND_RASTER_TEX_SVG_COLOR, GPU_BIND_RASTER_TEX_SLOT0, GPU_BIND_GLYPH_CURVES, GPU_BIND_GLYPH_CELL_ORIGIN,
            GPU_BIND_GLYPH_CELL_SIZE, GPU_BIND_GLYPH_SEG_COUNT, GPU_BIND_GLYPH_TEX_ATLAS, GPU_BIND_COLOR_SVG_CURVES,
            GPU_BIND_COLOR_SVG_SHAPES, GPU_BIND_COLOR_SVG_CELL_ORIGIN, GPU_BIND_COLOR_SVG_CELL_SIZE,
            GPU_BIND_COLOR_SVG_SHAPE_COUNT, GPU_BIND_COLOR_SVG_TEX_ATLAS);
        // A truncated preamble drops whichever defines fell off the end, and the shaders then fail to compile
        // against names that no longer exist.
        if (written < 0 || (size_t)written >= sizeof buf) {
            log_error("gpu backend: shader #define preamble truncated (%d bytes needed, %zu available)", written,
                      sizeof buf);
        }
        built = 1;
    }
    return buf;
}

int gpu_classify_sources(const char** out) {
    int n = 0;
    out[n++] = gpu_shader_preamble();
    out[n++] = gpu_glsl_shader_compat;
    out[n++] = gpu_shader_defines();
    out[n++] = gpu_glsl_tile_defs;
    out[n++] = gpu_glsl_kind_defs;
    out[n++] = gpu_glsl_drawcmd;
    out[n++] = gpu_glsl_segment;
    out[n++] = gpu_glsl_classify;
    return n;
}

int gpu_raster_sources(const char** out) {
    int n = 0;
    out[n++] = gpu_shader_preamble();
    out[n++] = gpu_glsl_shader_compat;
    out[n++] = gpu_shader_defines();
    out[n++] = gpu_glsl_tile_defs;
    out[n++] = gpu_glsl_kind_defs;
    out[n++] = gpu_glsl_drawcmd;
    out[n++] = gpu_glsl_segment;
    out[n++] = gpu_glsl_raster;
    return n;
}

// Glyph and color SVG are standalone coverage rasters: past the shared kernel they use none of the tile / atlas
// constants, but they take the same #define preamble for its binding indices.
int gpu_glyph_sources(const char** out) {
    int n = 0;
    out[n++] = gpu_shader_preamble();
    out[n++] = gpu_glsl_shader_compat;
    out[n++] = gpu_shader_defines();
    out[n++] = gpu_glsl_coverage_kernel;
    out[n++] = gpu_glsl_glyph;
    return n;
}

// Test seam: swaps the color-svg body for a source that cannot compile, so create_gpu_renderer sees a
// partial compile failure (three programs succeed, one fails).
static bool g_force_color_svg_failure = false;

void gpu_shaders_test_force_color_svg_failure(bool enable) {
    g_force_color_svg_failure = enable;
}

int gpu_color_svg_sources(const char** out) {
    int n = 0;
    out[n++] = gpu_shader_preamble();
    out[n++] = gpu_glsl_shader_compat;
    out[n++] = gpu_shader_defines();
    out[n++] = gpu_glsl_coverage_kernel;
    out[n++] = g_force_color_svg_failure ? "#error forced compile failure (test seam)\n" : gpu_glsl_color_svg;
    return n;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The Viewport3d pair: hardware vertex/fragment stages, so they take only the dialect preamble - neither the
// compute compat header nor the #define preamble applies, and the pair is GL only.

int gpu_mesh3d_vertex_sources(const char** out) {
    int n = 0;
    out[n++] = gpu_shader_preamble();
    out[n++] = gpu_glsl_mesh3d_vert;
    return n;
}

int gpu_mesh3d_fragment_sources(const char** out) {
    int n = 0;
    out[n++] = gpu_shader_preamble();
    out[n++] = gpu_glsl_mesh3d_frag;
    return n;
}
