#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The shader resource-binding contract: every buffer and texture index the host encoder sets and the shader
// signature declares, named once. gpu_shaders.c splices these into every program's source through the generated
// #define preamble, so the shader spells BIND_RASTER_CLIPS where the host spells
// GPU_BIND_RASTER_CLIPS.
//
// The indices are per-program, not global: index 3 is classify's cmd_count and raster's clip array, index 5 is
// classify's tile_rows and raster's screen_size. So each constant carries its program's name - a resource is only
// ever bound with the constant belonging to the program the encoder just set.
//
// The GLSL declarations (the #ifndef FL_METAL branch of each shader) keep their literal binding = N, matching
// the GL host binding by literal (glBindBufferBase / glBindImageTexture in gpu_renderer.c).

// Pass 1: classification (classify.glsl / classify_main).
#define GPU_BIND_CLASSIFY_CMDS 0
#define GPU_BIND_CLASSIFY_AABBS 1
#define GPU_BIND_CLASSIFY_TILE_LISTS 2
#define GPU_BIND_CLASSIFY_CMD_COUNT 3
#define GPU_BIND_CLASSIFY_TILE_COLS 4
#define GPU_BIND_CLASSIFY_TILE_ROWS 5

// Pass 2: raster (raster.glsl / raster_main). Buffers 0..8, textures 0..5 (the slot array runs 5..5+SLOT_COUNT-1).
#define GPU_BIND_RASTER_CMDS 0 // shared with classify at the same index
#define GPU_BIND_RASTER_TILE_LISTS 2
#define GPU_BIND_RASTER_CLIPS 3
#define GPU_BIND_RASTER_TILE_COLS 4
#define GPU_BIND_RASTER_SCREEN_SIZE 5
#define GPU_BIND_RASTER_DEBUG_MODE 6
#define GPU_BIND_RASTER_SLOT_INV_SIZE 7 // per-slot reciprocal texture extent (uv normalization)
#define GPU_BIND_RASTER_SLOT_FILTER 8   // per-slot filter mode (Metal has no sampler object on the texture)

#define GPU_BIND_RASTER_TEX_TARGET 0    // the offscreen RGBA8 target (write image)
#define GPU_BIND_RASTER_TEX_GLYPH 1     // the R8 glyph coverage atlas
#define GPU_BIND_RASTER_TEX_IMAGE 2     // the RGBA8 image atlas
#define GPU_BIND_RASTER_TEX_SVG 3       // the R8 monochrome SVG atlas
#define GPU_BIND_RASTER_TEX_SVG_COLOR 4 // the RGBA8 multi-color SVG atlas
#define GPU_BIND_RASTER_TEX_SLOT0 5     // base of the slot-image sampler array (GPU_IMAGE_SLOT_COUNT elements)

// Glyph coverage raster (glyph.glsl / glyph_main), also driving the monochrome SVG cell.
#define GPU_BIND_GLYPH_CURVES 0      // the lowered quadratics (3 float2 per segment)
#define GPU_BIND_GLYPH_CELL_ORIGIN 1 // the padded cell's texel position in the atlas
#define GPU_BIND_GLYPH_CELL_SIZE 2   // the padded cell's extent
#define GPU_BIND_GLYPH_SEG_COUNT 3
#define GPU_BIND_GLYPH_TEX_ATLAS 0 // the atlas the cell is image-stored into (glyph or monochrome SVG)

// Multi-color SVG raster (color_svg.glsl / color_svg_main): the glyph program's shape plus the per-shape records,
// which push the cell uniforms one index along.
#define GPU_BIND_COLOR_SVG_CURVES 0      // every shape's quadratics, concatenated
#define GPU_BIND_COLOR_SVG_SHAPES 1      // one record per shape (quad range, premultiplied color, fill rule)
#define GPU_BIND_COLOR_SVG_CELL_ORIGIN 2 // the padded cell's texel position in the atlas
#define GPU_BIND_COLOR_SVG_CELL_SIZE 3   // the padded cell's extent
#define GPU_BIND_COLOR_SVG_SHAPE_COUNT 4
#define GPU_BIND_COLOR_SVG_TEX_ATLAS 0 // the RGBA8 SVG atlas the cell is image-stored into
