#include <flowi_core/image.h>
#include <stb_image.h>
#include "image_private.h"
#include "internal.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// load image from file or memory

static FlImage load_image(struct FlContext* ctx, FlString filename, u8* data, u32 size) {
    int x = 0;
    int y = 0;
    int channels_in_file = 0;
    u8* image_data = NULL;

    // if data is set we assume that we are going to load from memory
    if (data) {
        image_data = stbi_load_from_memory(data, size, &x, &y, &channels_in_file, 4);
    } else {
        if (!filename.c_string) {
            // TODO: Implement non-cstring to temp buffer
            printf("failed to load image: fix nullterm\n");
            return 0;
        }

        image_data = stbi_load(filename.str, &x, &y, &channels_in_file, 4);
    }

    if (!image_data) {
        // TODO: Handle case where string is not null-terminated
        ERROR_ADD(FlError_Image, "Unable to load %s", filename.str);
        return 0;
    }

    // TODO: Needs lock in MT_CONFIG
    // Lock_lock(flowi_ctx->global_state->lock);
    ImagePrivate* image = Handles_create_handle(&ctx->global->image_handles);
    // Lock_unlock(flowi_ctx->global_state->lock);
    FL_TRY_ALLOC_INT(image);
    // Fill image data
    // TODO: Currenty assumes 4 bytes per pixel
    // TODO: Make sure we pick the correct texture format
    image->data = image_data;
    image->info.width = x;
    image->info.height = y;
    image->format = FlTextureFormat_Rgb8Srgb;

    return image->handle;
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
    // Lock_lock(flowi_ctx->global_state->lock);
    ImagePrivate* data = Handles_get_data(&ctx->global->image_handles, self);
    // Lock_unlock(flowi_ctx->global_state->lock);

    if (!data) {
        ERROR_ADD(FlError_Image, "Invalid handle %s", "todo: filename");
        return NULL;
    }

    return &data->info;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_image_destroy_impl(struct FlContext* ctx, FlImage image) {
    // Lock_lock(flowi_ctx->global_state->lock);
    ImagePrivate* image_data = Handles_get_data(&ctx->global->image_handles, image);
    // Lock_unlock(flowi_ctx->global_state->lock);

    if (!image_data) {
        ERROR_ADD(FlError_Image, "Invalid handle %s", "todo name");
        return;
    }

    stbi_image_free(image_data->data);
    // Lock_lock(flowi_ctx->global_state->lock);
    Handles_remove_handle(&ctx->global->image_handles, image);
    // Lock_unlock(flowi_ctx->global_state->lock);
}
