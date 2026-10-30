#pragma once

#include <stdbool.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GPU backend: the compute-shader GLSL program sources. Four programs: classification then raster (both over the
// draw-command/AABB/tile/clip SSBOs), the per-glyph coverage raster, and the multi-color SVG raster.
//
// The GLSL lives in shaders/*.glsl and is embedded as byte arrays by the build (cmake/embed_glsl.cmake). Each
// program is composed at compile time, in this order: a per-backend dialect preamble (the "#version" line on GL),
// the shared-vocabulary compat header (shader_compat_glsl.h), a generated "#define" preamble splicing the host's
// constants (tile geometry, atlas sizes, slot count), the shared GLSL snippets, and the shader body. The sources
// functions below fill the array glShaderSource takes.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Upper bound on the source-string count any program assembles - sizes the array passed to the fillers below.
#define GPU_SHADER_MAX_SOURCES 9

// Fill out (capacity GPU_SHADER_MAX_SOURCES) with one program's ordered source strings and return the count, ready
// to hand to glShaderSource.

// Pass 1: classification - one thread per tile, building each tile's command index list.
int gpu_classify_sources(const char** out);

// Pass 2: raster - one workgroup per tile, one thread per pixel, blending the tile's classified commands.
int gpu_raster_sources(const char** out);

// Glyph coverage raster: one dispatch per prepared glyph, signed-area coverage into the R8 atlas.
int gpu_glyph_sources(const char** out);

// Multi-color SVG raster: one dispatch per prepared multi-color icon, compositing its shapes in document order into
// the RGBA8 SVG atlas. The Curves SSBO (binding 0) holds all shapes' quads; the Shapes SSBO (binding 1) the
// per-shape (quad range, premultiplied color, fill rule) records.
int gpu_color_svg_sources(const char** out);

// The Viewport3d unlit vertex and fragment stages - the backend's only hardware raster program. Both take just the
// dialect preamble; neither the compat header nor the #define preamble applies to them.
int gpu_mesh3d_vertex_sources(const char** out);
int gpu_mesh3d_fragment_sources(const char** out);

// Test-only: when enabled, gpu_color_svg_sources substitutes an intentionally invalid body, so one program fails to
// compile while the other three succeed.
void gpu_shaders_test_force_color_svg_failure(bool enable);
