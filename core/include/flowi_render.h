#pragma once

#include "../src/types.h"

// TODO: This should be configurable
#define FLI_INDEX_SIZE 2

#if FLI_INDEX_SIZE == 2
typedef u16 FliIdxSize;
#elif FLI_INDEX_SIZE == 4
typedef u32 FliIdxSize;
#else
#error "Unsupported index size. Only u16 or u32 supported"
#endif

typedef struct FliVertPosColor {
	float x,y;
	u32 color;
} FliVertPosColor;

typedef struct FliVertPosUvColor {
	float x,y;
	float u,v;
	u32 color;
} FliVertPosUvColor;

typedef struct FliIntRect {
    int x0, y0;
    int x1, y1;
} FliIntRect;

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
typedef struct FliRcTexturedTriangles {
    // Lifetime: Renderer has to take a copy of this.
    FliVertPosUvColor* vertex_buffer;
    // Lifetime: Renderer has to take a copy of this.
    FliIdxSize* index_buffer;
    // number of vertices
    u32 vertex_count;
    // number of triangles in the buffer
    u32 triangle_count;
} FliRcTexturedTriangles;

// Used for rendering non-textured triangles (doesn't have to be single color) as the shader may generate colors itself
typedef struct FliRcSolidTriangles {
    // Lifetime: Renderer has to take a copy of this.
    FliVertPosColor* vertex_buffer;
    // Lifetime: Renderer has to take a copy of this.
    FliIdxSize* index_buffer;
    // number of vertices
    u32 vertex_count;
    // number of triangles in the buffer
    u32 triangle_count;
} FliRcSolidTriangles;

// Texture format specificed when using FliRc_CreateTexture command
typedef enum FliTextureFormat {
    // Single byte texture in linear format
    FliTextureFormat_R8_LINEAR,
    // 3 byte R,G,B format (sRGB)
    FliTextureFormat_RGB8_sRGB,
    // 3 byte R,G,B format (LINEAR)
    FliTextureFormat_RGB8_LINEAR,
    // 4 byte R,G,B,A format (sRGB)
    FliTextureFormat_RGBA8_sRGB,
    // 4 byte R,G,B,A format (LINEAR)
    FliTextureFormat_RGBA8_LINEAR,
    // 16-bit single format. This will mostly be used for temporary things such as blurs that requires more
    // than one pass rendering. i16 or f16 will allow better accuracy, but R8_LINEAR can be used in worst case
    // in case the rendering backend doesn't support this format.
    FliTextureFormat_I16_OR_F16_LINEAR,
} FliTextureFormat;

// Used when Flowi needs to create a texture for some purpose (can be for fonts, temporary render-targets, etc)
// Notice that fli_create(...) allow the set the maximum number of textures/sizes that flowi is allowed to use.
// If Flowi is unable to to live within the bounds of the restrictions given it will stop updating and the user
// has to use fli_get_status() to check what the issue is.
typedef struct FliRcCreateTexture {
    // This is the id that will later be used when refering to the texture
    u16 texture_id;
    // See FliTextureFormat for the type
    u16 texture_format;
    // width of the texture
    u16 width;
    // height of the texture
    u16 height;
} FliRcCreateTexture;

// This command is used to copy some data from the CPU to the GPU side. This is mostly used
// for images/fonts/etc that needs to endup in a texture on the GPU. There is also a FliRcBlitRect
// for blitting between GPU targets (this may be used for example when scaling down content for bluring or other reasons)
typedef struct FliRcCpuBlitRect {
    // Lifetime: The GPU backend can assume this will be alive for at least 3 times of GPU-frames.
    u8* source_data;
    // Source rectangle to copy from
    FliIntRect source_rect;
    // Destination rectangle on the GPU Side
    FliIntRect dest_rect;
    // Target for the blit
    u16 texture_id;
} FliRcCpuBlitRect;

// Copy data between GPU targets.
typedef struct FliRcGpuBlitRect {
    // Source rectangle to copy from
    FliIntRect source_rect;
    // Destination rectangle on the GPU Side
    FliIntRect dest_rect;
    // Source for the texture
    u16 source_texture_id;
    // Target Texture Id
    u16 target_texture_id;
} FliRcGpuBlitRect;

// FliRenderCommands that the render backend needs to support
typedef enum FliRenderCommand {
    FliRc_RenderTexturedTriangles,
    FliRc_RenderTriangles,
    FliRc_CreateTexture,
    FliRc_CpuBlitRect,
    FliRc_GpuBlitRect,
} FliRenderCommand;

// Return from Flowi to be used by a rendering backend to render data
typedef struct FliRenderData {
    // list of render command. These values should be cast to FliRenderCommand
    const u8* render_commands;
    // Data for the render commands. Needs to be cast to the correct type depending on enum
    const u8* render_data;
    // Total number of render commands
    int render_command_count;
} FliRenderData;
