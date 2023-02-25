
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This file is auto-generated by api_gen. DO NOT EDIT!
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "manual.h"


#ifdef __cplusplus
extern "C" {
#endif

// Texture format specificed when using [CreateTexture] command
typedef enum FlTextureFormat {
    // Single byte texture in linear format
    FlTextureFormat_R8Linear = 0,
    // 3 byte R,G,B format (sRGB)
    FlTextureFormat_Rgb8Srgb = 1,
    // 3 byte R,G,B format (LINEAR)
    FlTextureFormat_Rgb8Linear = 2,
    // 4 byte R,G,B,A format (sRGB)
    FlTextureFormat_Rgba8Srgb = 3,
    // 4 byte R,G,B,A format (LINEAR)
    FlTextureFormat_Rgba8Linear = 4,
    // 16-bit single format. This will mostly be used for temporary things such as blurs that requires more
    // than one pass rendering. i16 or f16 will allow better accuracy, but R8_LINEAR can be used in worst case
    // in case the rendering backend doesn't support this format
    FlTextureFormat_I16OrF16Linear = 5,
} FlTextureFormat;

// Used when specifying rect updates
typedef struct FlRenderRect {
    int x0;
    int y0;
    int x1;
    int y1;
} FlRenderRect;

// Vertex layout for textured triangles
typedef struct FlVertPosUvColor {
    float x;
    float y;
    uint16_t u;
    uint16_t v;
    uint32_t color;
} FlVertPosUvColor;

// Vertex layout for solid triangles
typedef struct FlVertPosColor {
    float x;
    float y;
    uint32_t color;
} FlVertPosColor;

// Used for rendering triangles with a texture.
typedef struct FlTexturedTriangles {
    // Offset into the index buffer 
    uint32_t offset;
    // Vertices for the command
    FlVertPosUvColor* vertex_buffer;
    uint32_t vertex_buffer_size;
    // Index buffer for the command
    uint16_t* index_buffer;
    uint32_t index_buffer_size;
    // Texture id used for the command
    uint32_t texture_id;
} FlTexturedTriangles;

typedef struct FlSolidTriangles {
    // Offset into the index buffer 
    uint32_t offset;
    // Vertices for the command
    FlVertPosColor* vertex_buffer;
    uint32_t vertex_buffer_size;
    // Index buffer for the command
    uint16_t* index_buffer;
    uint32_t index_buffer_size;
} FlSolidTriangles;

typedef struct FlCreateTexture {
    // Data upload (can be NULL if data is uploaded later)
    uint8_t* data;
    uint32_t data_size;
    // This is the id that will later be used when refering to the texture
    uint16_t id;
    // See [TextureFormat] for the type
    uint16_t format;
    // width of the texture
    uint16_t width;
    // height of the texture
    uint16_t height;
} FlCreateTexture;

// This is used to update an existing texture with some data. This usually happens when a new image/glyph/etc
// needs to be displayed but isn't present in a texture yet
typedef struct FlUpdateTexture {
    // Data to upload
    uint8_t* data;
    uint32_t data_size;
    // area to update
    FlRenderRect rect;
    // Texture to update
    uint16_t texture_id;
} FlUpdateTexture;

// Used when restricting an area for rendering. How this is to be implemented depends onthe GPU
// API, but for OpenGL this corresponts to https://www.khronos.org/registry/OpenGL-Refpages/es2.0/xhtml/glScissor.xml
typedef struct FlScissorRect {
    // Area restricted for rendering
    FlRenderRect rect;
} FlScissorRect;


#include "render_commands.inl"

#ifdef __cplusplus
}
#endif

// Commands that will be in the render stream
typedef enum FlRenderCommand {
    FlRenderCommand_TexturedTriangles,
    FlRenderCommand_SolidTriangles,
    FlRenderCommand_CreateTexture,
    FlRenderCommand_UpdateTexture,
    FlRenderCommand_ScissorRect,
} FlRenderCommand;

