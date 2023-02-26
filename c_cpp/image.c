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

    ImagePrivate* image = Handles_create_handle(&ctx->global->image_handles);
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

    ImagePrivate* image = Handles_create_handle(&ctx->global->image_handles);

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ImagePrivate* get_handle(FlInternalData* ctx, FlImage self) {
    ImagePrivate* data = Handles_get_data(&ctx->global->image_handles, self);

    if (!data) {
        ERROR_ADD(FlError_Image, "Invalid handle for image: %lx, has it been deleted?", self);
        return NULL;
    }

    return data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlImage create_from_file(FlInternalData* ctx, FlString filename) {
    return load_image(ctx, filename, NULL, 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlImageInfo* get_info(FlInternalData* ctx, FlImage self) {
    ImagePrivate* data = NULL;

    if (!(data = get_handle(ctx, self))) {
        return NULL;
    }

    return &data->info;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Image_add_to_atlas(const u8* cmd, struct Atlas* atlas) {
    PrimitiveImage* prim = (PrimitiveImage*)cmd;
    ImagePrivate* self = prim->image;

    // If we have already upload the image we can skip this step
    if (self->texture_id != 0) {
        return true;
    }

    int rx = 0, ry = 0, stride = 0;

    int width = prim->size.x;
    int height = prim->size.y;

    u8* dest = Atlas_add_rect(atlas, width, height, &rx, &ry, &stride);
    if (!dest) {
        ERROR_ADD(FlError_Image, "Unable to add %s to atlas. Likely out of space", self->name.str);
        return false;
    }

    printf("adding to atlas at pos (%d %d : %d %d)\n", rx, ry, width, height);

    if (self->svg_image) {
        if (!self->svg_raster) {
            self->svg_raster = nsvgCreateRasterizer();
        }

        // int width = self->info.width;
        // int height = self->info.height;

        int width = prim->size.x;
        int height = prim->size.y;

        // raster only takes one scale value so we use x
        float scale_x = (float)self->info.width / (float)self->svg_image->width;
        float scale = scale_x;

        //dest += (ry * stride) + (rx * 4);

        printf("Rasterizing svg to %f %d %d %d\n", scale, width, height, stride);

        nsvgRasterize(self->svg_raster, self->svg_image, 0.0f, 0.0f, scale, dest, width, height, stride);

        // As the above function alwyas render in RGBA we move over the alpha to all channels here if
        // that is the mode we want
        if (self->svg_flags == FlSvgFlags_Alpha) {
            const int h = self->info.height;
            const int w = self->info.width;

            uint8_t* t = dest;

            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    t[0] = t[3];
                    t[1] = t[3];
                    t[2] = t[3];
                    t += 4;
                }

                t += stride - (w * 4);
            }
        }
    } else {
        // Copy the the image data to the atlas
        // TODO: not doing a copy and only use the atlas for virtual data and copy directly from
        //       image data instead
        const u8* src = self->data;
        const int image_stride = self->info.width * 4;  // TODO: Calculate multiply

        for (int h = 0, height = self->info.height; h < height; ++h) {
            memcpy(dest, src, image_stride);
            src += image_stride;
            dest += stride;
        }

        src = self->data;
    }

    float inv_width = 1.0f / (float)atlas->width;
    float inv_height = 1.0f / (float)atlas->height;

    self->u0 = (float)rx * inv_width;
    self->v0 = (float)ry * inv_height;
    self->u1 = (float)(rx + width) * inv_width;
    self->v1 = (float)(ry + height) * inv_height;

    printf("Image %s added to atlas at %d %d %d %d\n", self->name.str, rx, ry, width, height);

    // Setup u0, v0, u1, v1 for faster access when using ImGui::Image

    self->texture_id = atlas->texture_id;
    self->atlas_x = rx;
    self->atlas_y = ry;

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*

static void destroy(FlInternalData* ctx, FlImage image) {
    ImagePrivate* image_data = Handles_get_data(&ctx->global->image_handles, image);

    if (!image_data) {
        ERROR_ADD(FlError_Image, "Invalid handle %s", "todo name");
        return;
    }

    // INFO: No need to free string name here as it's beeing freed at the cleanup of the context

    stbi_image_free(image_data->data);
    nsvgDelete(image_data->svg_image);
    nsvgDeleteRasterizer(image_data->svg_raster);

    Handles_remove_handle(&ctx->global->image_handles, image);
}
*/

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FlImageApi g_image_funcs = {
    NULL,
    create_from_file,
    create_from_memory,
    create_svg_from_file,
    create_svg_from_memory,
    get_info,
    //destroy,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FlImageApi* fl_image_get_api(FlInternalData* ctx, int api_version) {
    FL_UNUSED(api_version);
    return &ctx->image_funcs;
}


