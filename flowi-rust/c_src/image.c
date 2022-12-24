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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// load image from file or memory

static FlImage load_image(struct FlContext* ctx, FlString name, u8* data, u32 size) {
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

static FlImage load_svg_image(struct FlContext* ctx, FlString name, u8* data, u32 data_size, u32 target_width, 
                              FlSvgFlags flags) {
    char temp_buffer[2048];
    NSVGimage* svg_image = NULL;
    const char* filename =
        StringAllocator_temp_string_to_cstr(&ctx->string_allocator, temp_buffer, sizeof(temp_buffer), name);

    // if data is set we assume that we are going to load from memory
    if (data) {
        svg_image = nsvgParse((char*)data, "px", 96);
    } else {
        svg_image = nsvgParseFromFile(filename, "px", 96);
    }

    if (!svg_image) {
        // TODO: Handle case where string is not null-terminated
        ERROR_ADD(FlError_Image, "Unable to load %s", filename);
        return 0;
    }

    ImagePrivate* image = Handles_create_handle(&ctx->global->image_handles);

    float scale = (float)target_width / (float)svg_image->width;
    int y = (int)(svg_image->height * scale);

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

static ImagePrivate* get_handle(FlContext* ctx, FlImage self) {
    ImagePrivate* data = Handles_get_data(&ctx->global->image_handles, self);

    if (!data) {
        ERROR_ADD(FlError_Image, "Invalid handle for image: %lx, has it been deleted?", self);
        return NULL;
    }

    return data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlImage fl_image_create_from_file_impl(struct FlContext* ctx, FlString filename) {
    return load_image(ctx, filename, NULL, 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlImage fl_image_create_from_memory_impl(struct FlContext* ctx, FlString name, uint8_t* data, uint32_t data_size) {
    return load_image(ctx, name, data, data_size);
}

// Load SVG from file
FlImage fl_image_create_svg_from_file_impl(struct FlContext* ctx, FlString filename, uint32_t target_width,
                                           FlSvgFlags flags) {
    return load_svg_image(ctx, filename, NULL, 0, target_width, flags);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Load SVG from memory

FlImage fl_image_create_svg_from_memory_impl(struct FlContext* ctx, FlString name, uint8_t* data, uint32_t data_size,
                                             uint32_t target_width, FlSvgFlags flags) {
    return load_svg_image(ctx, name, data, data_size, target_width, flags);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlImageInfo* fl_image_get_info_impl(struct FlContext* ctx, FlImage self) {
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

#if 0

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_image_destroy_impl(struct FlContext* ctx, FlImage image) {
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Image_render(struct FlContext* ctx, const u8* cmd) {
    (void)ctx;
    (void)cmd;
/*
    PrimitiveImage* prim = (PrimitiveImage*)cmd;

    FlVertPosUvColor* vertices = NULL;
    FlIdxSize* index_buffer = NULL;

    VertexAllocator_alloc_pos_uv_color(&ctx->vertex_allocator, &vertices, &index_buffer, 4, 6);

    // TODO: fixme
    u32 color = 0xffffffff;

    u16 x0 = (u16)prim->image->atlas_x;
    u16 y0 = (u16)prim->image->atlas_y;
    u16 x1 = (u16)(x0 + prim->size.x);
    u16 y1 = (u16)(y0 + prim->size.y);

    u16 u0 = (u16)prim->image->atlas_x;
    u16 v0 = (u16)prim->image->atlas_y;
    u16 u1 = (u16)(u0 + prim->size.x);
    u16 v1 = (u16)(v0 + prim->size.y);
    // u16 u1 = (u16)(u0 + prim->image->info.width);
    // u16 u1 = (u16)(u0 + prim->image->info.width);

    float rx = prim->position.x;
    float ry = prim->position.y;

    float nx0 = rx;
    float ny0 = ry;
    float nx1 = rx + (x1 - x0);
    float ny1 = ry + (y1 - y0);

    vertices[0].x = nx0;
    vertices[0].y = ny0;
    vertices[0].u = u0;
    vertices[0].v = v0;
    vertices[0].color = color;

    vertices[1].x = nx1;
    vertices[1].y = ny0;
    vertices[1].u = u1;
    vertices[1].v = v0;
    vertices[1].color = color;

    vertices[2].x = nx1;
    vertices[2].y = ny1;
    vertices[2].u = u1;
    vertices[2].v = v1;
    vertices[2].color = color;

    vertices[3].x = nx0;
    vertices[3].y = ny1;
    vertices[3].u = u0;
    vertices[3].v = v1;
    vertices[3].color = color;

    // TODO: Shouldn't hardcode to start with index 0
    index_buffer[0] = 0;
    index_buffer[1] = 1;
    index_buffer[2] = 2;

    index_buffer[3] = 0;
    index_buffer[4] = 2;
    index_buffer[5] = 3;

    FlTexturedTriangles* tri_data = Render_textured_triangles_cmd(ctx->global);

    tri_data->offset = ctx->vertex_allocator.index_offset;
    tri_data->vertex_buffer = vertices;
    tri_data->index_buffer = index_buffer;
    tri_data->vertex_buffer_size = 4;
    tri_data->index_buffer_size = 6;
    tri_data->texture_id = prim->image->texture_id;
*/

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_ui_image_with_size_impl(struct FlContext* ctx, FlImage image, FlVec2 size) {
    (void)ctx;
    (void)image;
    (void)size;
    /*
    ImagePrivate* self = get_handle(ctx, image);

    if (FL_UNLIKELY(!self)) {
        return;
    }

    Layer* layer = ctx_get_active_layer(ctx);

    PrimitiveImage* prim = Primitive_alloc_image(layer);

    prim->image = self;
    prim->position = ctx->cursor;
    prim->size.x = size.x;
    prim->size.y = size.y;
    */
}



#endif
