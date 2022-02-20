#include <flowi_core/image.h>
#include <stb_image.h>
#include "atlas.h"
#include "image_private.h"
#include "internal.h"
#include "string_allocator.h"
#include "vertex_allocator.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// load image from file or memory

static FlImage load_image(struct FlContext* ctx, FlString name, u8* data, u32 size) {
    int x = 0;
    int y = 0;
    int channels_in_file = 0;
    u8* image_data = NULL;

    char temp_buffer[2048];
    const char* filename =
        StringAllocator_temp_string_to_cstr(&ctx->string_allocator, temp_buffer, sizeof(temp_buffer), name);

    if (!filename) {
        // TODO: Handle case where string is not null-terminated
        ERROR_ADD(FlError_Image, "Unable to load: %s", "fixme");
        return 0;
    }

    // if data is set we assume that we are going to load from memory
    if (data) {
        image_data = stbi_load_from_memory(data, size, &x, &y, &channels_in_file, 4);
    } else {
        image_data = stbi_load(filename, &x, &y, &channels_in_file, 4);
    }

    if (!image_data) {
        // TODO: Handle case where string is not null-terminated
        ERROR_ADD(FlError_Image, "Unable to load %s", filename);
        return 0;
    }

    ImagePrivate* image = Handles_create_handle(&ctx->global->image_handles);
    FL_TRY_ALLOC_INT(image);
    // Fill image data
    // TODO: Currenty assumes 4 bytes per pixel
    // TODO: Make sure we pick the correct texture format
    image->data = image_data;
    image->info.width = x;
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlImageInfo* fl_image_get_info_impl(struct FlContext* ctx, FlImage self) {
    ImagePrivate* data = NULL;

    FL_TRY_ALLOC_NULL(data = get_handle(ctx, self));

    return &data->info;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_ui_image_impl(struct FlContext* ctx, FlImage image) {
    ImagePrivate* self = get_handle(ctx, image);

    if (FL_UNLIKELY(!self)) {
        return;
    }

    PrimitiveImage* prim = Primitive_alloc_image(ctx->global);

    if (FL_UNLIKELY(!prim)) {
        ERROR_ADD(FlError_Font, "unable to draw from texture: %s out of memory in primitive allocator: %s",
                  self->name.str);
        return;
    }

    prim->image = self;
    prim->position = ctx->cursor;
    prim->size.x = self->info.width;
    prim->size.y = self->info.height;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_image_destroy_impl(struct FlContext* ctx, FlImage image) {
    ImagePrivate* image_data = Handles_get_data(&ctx->global->image_handles, image);

    if (!image_data) {
        ERROR_ADD(FlError_Image, "Invalid handle %s", "todo name");
        return;
    }

    // INFO: No need to free string name here as it's beeing freed at the cleanup of the context

    stbi_image_free(image_data->data);
    Handles_remove_handle(&ctx->global->image_handles, image);
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

    u8* dest = Atlas_add_rect(atlas, self->info.width, self->info.height, &rx, &ry, &stride);
    if (!dest) {
        ERROR_ADD(FlError_Image, "Unable to add %s to atlas. Likely out of space", self->name.str);
        return false;
    }

    // Copy the the image data to the atlas
    // TODO: not doing a copy and only use the atlas for virtual data and copy directly from
    //       image data instead

    const u8* src = self->data;

    for (int h = 0, height = self->info.height; h < height; ++h) {
        memcpy(dest, src, self->info.width * 4);
        src += self->info.width * 4;  // TODO: Calculate multiply
        dest += stride / 4;
    }

    self->texture_id = atlas->texture_id;
    self->atlas_x = rx;
    self->atlas_y = ry;

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Image_render(struct FlContext* ctx, const u8* cmd) {
    PrimitiveImage* prim = (PrimitiveImage*)cmd;

    FlVertPosUvColor* vertices = NULL;
    FlIdxSize* index_buffer = NULL;

    if (!VertexAllocator_alloc_pos_uv_color(&ctx->vertex_allocator, &vertices, &index_buffer, 4, 6)) {
        return false;
    }

    // TODO: fixme
    u32 color = 0xffffffff;

    u16 x0 = (u16)prim->image->atlas_x;
    u16 y0 = (u16)prim->image->atlas_y;
    u16 x1 = (u16)(x0 + prim->size.x);
    u16 y1 = (u16)(y0 + prim->size.y);

    float rx = prim->position.x;
    float ry = prim->position.y;

    float nx0 = rx;
    float ny0 = ry;
    float nx1 = rx + (x1 - x0);
    float ny1 = ry + (y1 - y0);

    vertices[0].x = nx0;
    vertices[0].y = ny0;
    vertices[0].u = x0;
    vertices[0].v = y0;
    vertices[0].color = color;

    vertices[1].x = nx1;
    vertices[1].y = ny0;
    vertices[1].u = x1;
    vertices[1].v = y0;
    vertices[1].color = color;

    vertices[2].x = nx1;
    vertices[2].y = ny1;
    vertices[2].u = x1;
    vertices[2].v = y1;
    vertices[2].color = color;

    vertices[3].x = nx0;
    vertices[3].y = ny1;
    vertices[3].u = x0;
    vertices[3].v = y1;
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

    return true;
}
