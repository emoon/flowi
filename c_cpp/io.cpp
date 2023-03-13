#include <flowi/io.h>
#include <flowi/image.h>
#include <nanosvg.h>
#include <nanosvgrast.h>
#include <stb_image.h>
#include "atlas.h"
#include "image_private.h"
#include "internal.h"
#include "string_allocator.h"
#include "vertex_allocator.h"
#include "primitives.h"
#include "internal.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// load image from file or memory

static FlImage load_image(FlInternalData* ctx, FlString name, u8* data, u32 size) {
    int x = 0;
    int y = 0;
    int channels_in_file = 0;
    u8* image_data = NULL;
    NSVGimage* svg_image = NULL;

    char temp_buffer[2048];
    const char* filename =
        StringAllocator_temp_string_to_cstr(&ctx->string_allocator, temp_buffer, sizeof(temp_buffer), name);

    // if data is set we assume that we are going to load from memory
    if (data) {
        image_data = stbi_load_from_memory(data, size, &x, &y, &channels_in_file, 4);
    } else {
        image_data = stbi_load(filename, &x, &y, &channels_in_file, 4);
    }

    if (!image_data && !svg_image) {
        // TODO: Handle case where string is not null-terminated
        ERROR_ADD(FlError_Image, "Unable to load %s", filename);
        return 0;
    }

    ImagePrivate* image = (ImagePrivate*)Handles_create_handle(&ctx->global->image_handles);
    // Fill image data
    // TODO: Currenty assumes 4 bytes per pixel
    // TODO: Make sure we pick the correct texture format

    image->data = image_data;
    image->svg_image = svg_image;
    image->svg_raster = NULL;
    image->info.width = x;
    image->info.height = y;
    image->format = FlTextureFormat_Rgba8Srgb;
    image->texture_id = 0;
    image->name = StringAllocator_copy_string(&ctx->string_allocator, name);

    return image->handle;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
static FlImage load_svg_image(FlInternalData* ctx, FlString name, u8* data, u32 data_size, u32 target_width, 
                              FlSvgFlags flags) {
    char temp_buffer[2048];
    NSVGimage* svg_image = NULL;
    const char* filename =
        StringAllocator_temp_string_to_cstr(&ctx->string_allocator, temp_buffer, sizeof(temp_buffer), name);

    FL_UNUSED(data_size);

    // if data is set we assume that we are going to load from memory
    if (data) {
        svg_image = nsvgParse((char*)data, "px", 96);
    } else {
        svg_image = nsvgParseFromFile(filename, "px", 96);
    }

    if (!svg_image) {
        printf("Unable to load %s\n", filename);
        // TODO: Handle case where string is not null-terminated
        ERROR_ADD(FlError_Image, "Unable to load %s", filename);
        return 0;
    }

    ImagePrivate* image = (ImagePrivate*)Handles_create_handle(&ctx->global->image_handles);

    printf("SVG image size: %f %f\n", svg_image->width, svg_image->height);

    // print target_width and with
    printf("target_width: %d width: %f\n", target_width, svg_image->width);

    float scale = (float)target_width / (float)svg_image->width;
    int y = (int)(svg_image->height * scale);

    // print scale and y
    printf("scale: %f y: %d\n", scale, y);

    image->svg_image = svg_image;
    image->svg_raster = NULL;
    image->svg_flags = flags;
    image->info.width = target_width;
    image->info.height = y;
    image->format = FlTextureFormat_Rgba8Srgb;
    image->texture_id = 0;
    image->name = StringAllocator_copy_string(&ctx->string_allocator, name);

    return image->handle;
}
*/

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlImage create_from_file(FlInternalData* ctx, FlString filename) {
    return load_image(ctx, filename, NULL, 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*
static FlImage create_from_memory(FlInternalData* ctx, FlString name, uint8_t* data, uint32_t data_size) {
    return load_image(ctx, name, data, data_size);
}

// Load SVG from file
static FlImage create_svg_from_file(FlInternalData* ctx, FlString filename, uint32_t target_width, FlSvgFlags flags) {
    return load_svg_image(ctx, filename, NULL, 0, target_width, flags);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Load SVG from memory

static FlImage create_svg_from_memory(FlInternalData* ctx, FlString name, uint8_t* data, uint32_t data_size,
                                             uint32_t target_width, FlSvgFlags flags) {
    return load_svg_image(ctx, name, data, data_size, target_width, flags);
}
*/

extern "C" FlShader load_fragment_shader(FlInternalData* ctx, FlString name);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlIoApi g_io_funcs = {
    NULL,
    NULL,
    //load_fragment_shader,
    //create_from_file,
};
