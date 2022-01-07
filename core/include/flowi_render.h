#pragma once

#include "../src/types.h"

// TODO: This should be configurable
#define FLI_INDEX_SIZE 2

#if FLI_INDEX_SIZE == 2
typedef u16 FlIdxSize;
#elif FLI_INDEX_SIZE == 4
typedef u32 FlIdxSize;
#else
#error "Unsupported index size. Only u16 or u32 supported"
#endif

typedef struct FlVertPosColor {
	float x,y;
	u32 color;
} FlVertPosColor;

typedef struct FlVertPosUvColor {
	float x,y;
	u16 u,v;
	u32 color;
} FlVertPosUvColor;

typedef struct FlIntRect {
    int x0, y0;
    int x1, y1;
} FlIntRect;


// Used to describe the lifetime of data that is being rendered.
//
// FlMemoryLifetime_Static
// Static means that the data is alive for *at least* 3 frames of rendering update.
// If it's deleted/freed during that time the application will likely crash.
// Also if any updates happens to the data during this time it's undefined what the
// result will be on the GPU side
//
// FlMemoryLifetime_Temporary
// Temporary means that the memory will be freed after calling this function. The system
// will take a temporary copy and keep it alive for at least 3 frames so it can be safley
// tranfered to the GPU. This costs extra memory/allocations/etc so if Static can be used
// that is the preferred option.
//
typedef enum FlMemoryLifetime {
    FlMemoryLifetime_Static,
    FlMemoryLifetime_Temporary,
} FlMemoryLifetime;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This describes the API that the Rendering backend needs to implement.
//
// The way this works is that there is a stream of command and sub commands (more on this later)
//
// The data that comes here in two arrays. One being the type of command and the other being the data for that command
//
// As there are no callbacks it's up to the renderer to handle cases when there is a out of memory for example.
// Allocations happens for textures only. A CreateTexture command will contain an ID that will be refered to later when
// targeting that texture for various operations (such as blits/bind/etc) Flowi will assume that all of the operations
// have succused to the next update

// Used for rendering triangles with a texture.
typedef struct FlRcTexturedTriangles {
    // Lifetime: Renderer has to take a copy of this.
    FlVertPosUvColor* vertex_buffer;
    // Lifetime: Renderer has to take a copy of this.
    FlIdxSize* index_buffer;
    // Texture id to use when rendering
    u32 texture_id;
    // number of vertices
    u32 vertex_count;
    // number of indices in the buffer
    u32 index_count;
} FlRcTexturedTriangles;

// Used for rendering non-textured triangles (doesn't have to be single color) as the shader may generate colors itself
typedef struct FlRcSolidTriangles {
    // Lifetime: Renderer has to take a copy of this.
    FlVertPosColor* vertex_buffer;
    // Lifetime: Renderer has to take a copy of this.
    FlIdxSize* index_buffer;
    // number of vertices
    u32 vertex_count;
    // number of indices in the buffer
    u32 index_count;
} FlRcSolidTriangles;

// Texture format specificed when using FlRc_CreateTexture command
typedef enum FlTextureFormat {
    // Single byte texture in linear format
    FlTextureFormat_R8_LINEAR,
    // 3 byte R,G,B format (sRGB)
    FlTextureFormat_RGB8_sRGB,
    // 3 byte R,G,B format (LINEAR)
    FlTextureFormat_RGB8_LINEAR,
    // 4 byte R,G,B,A format (sRGB)
    FlTextureFormat_RGBA8_sRGB,
    // 4 byte R,G,B,A format (LINEAR)
    FlTextureFormat_RGBA8_LINEAR,
    // 16-bit single format. This will mostly be used for temporary things such as blurs that requires more
    // than one pass rendering. i16 or f16 will allow better accuracy, but R8_LINEAR can be used in worst case
    // in case the rendering backend doesn't support this format.
    FlTextureFormat_I16_OR_F16_LINEAR,
} FlTextureFormat;

// Used when Flowi needs to create a texture for some purpose (can be for fonts, temporary render-targets, etc)
// Notice that fli_create(...) allow the set the maximum number of textures/sizes that flowi is allowed to use.
// If Flowi is unable to to live within the bounds of the restrictions given it will stop updating and the user
// has to use fli_get_status() to check what the issue is.
typedef struct FlRcCreateTexture {
    // Data upload (can be NULL if data is uploaded later)
    u8* data;
    // This is the id that will later be used when refering to the texture
    u16 id;
    // See FlTextureFormat for the type
    u16 format;
    // width of the texture
    u16 width;
    // height of the texture
    u16 height;
} FlRcCreateTexture;

// This is used to update an existing texture with some data. This usually happens when a new image/glyph/etc
// needs to be displayed but isn't present in a texture yet
typedef struct FlRcUpdateTexture {
    // Lifetime: The GPU backend can assume this will be alive for at least FL_FRAME_HISTORY times of GPU-frames.
    u8* source_data;
    // Rect to be updated
    FlIntRect rect;
    // Target for the blit
    u16 texture_id;
} FlRcUpdateTexture;

// Copy data between GPU targets.
typedef struct FlRcGpuBlitRect {
    // Source rectangle to copy from
    FlIntRect source_rect;
    // Destination rectangle on the GPU Side
    FlIntRect dest_rect;
    // Source for the texture
    u16 source_texture_id;
    // Target Texture Id
    u16 target_texture_id;
} FlRcGpuBlitRect;

// FlRenderCommands that the render backend needs to support
typedef enum FlRenderCommand {
    FlRc_RenderTexturedTriangles,
    FlRc_RenderTriangles,
    FlRc_CreateTexture,
    FlRc_UpdateTexture,
    FlRc_GpuBlitRect,
} FlRenderCommand;

// Return from Flowi to be used by a rendering backend to render data
typedef struct FlRenderData {
    // Data for the render commands. Needs to be cast to the correct type depending on enum
    u8* render_data;
    // Total number of render commands
    int count;
} FlRenderData;

// Get the next render command
//u16 fl_render_get_next_command(struct FlGlobalState* state, const u8** ptr);

// History to keep buffers alive
#define FL_FRAME_HISTORY 2
