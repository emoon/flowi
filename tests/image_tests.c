#include <flowi_core/image.h>
#include <flowi_core/ui.h>
#include "../src/atlas.h"
#include "../src/internal.h"
#include "utest.h"

#if 0

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Image, load_file_ok_stb) {
    struct FlGlobalState* state = fl_create(NULL);
    struct FlContext* ctx = fl_context_create(state);

    FlImage id = fl_image_create_from_file(ctx, "data/uv.png");
    ASSERT_NE(0, id);

    fl_image_destroy(ctx, id);
    fl_context_destroy(ctx);
    fl_destroy(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Image, load_file_ok_svg) {
    struct FlGlobalState* state = fl_create(NULL);
    struct FlContext* ctx = fl_context_create(state);

    FlImage id = fl_image_create_from_file(ctx, "data/Freesample.svg");
    ASSERT_NE(0, id);

    fl_image_destroy(ctx, id);
    fl_context_destroy(ctx);
    fl_destroy(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Image, load_file_fail) {
    struct FlGlobalState* state = fl_create(NULL);
    struct FlContext* ctx = fl_context_create(state);

    FlImage id = fl_image_create_from_file(ctx, "data/no_such_file.png");
    ASSERT_EQ(0, id);

    fl_image_destroy(ctx, id);
    fl_context_destroy(ctx);
    fl_destroy(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Image, load_file_from_memory) {
    struct FlGlobalState* state = fl_create(NULL);
    struct FlContext* ctx = fl_context_create(state);

    u32 size = 0;
    u8* data = Io_load_file_to_memory(ctx, "data/uv.png", &size);
    ASSERT_NE(NULL, data);

    FlImage id = fl_image_create_from_memory(ctx, "uv", data, size);
    ASSERT_NE(0, id);

    FlAllocator_free(state->global_allocator, data);

    fl_image_destroy(ctx, id);
    fl_context_destroy(ctx);
    fl_destroy(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Image, load_file_from_memory_svg) {
    struct FlGlobalState* state = fl_create(NULL);
    struct FlContext* ctx = fl_context_create(state);

    u32 size = 0;
    u8* data = Io_load_file_to_memory_null_term(ctx, "data/Freesample.svg", &size);
    ASSERT_NE(NULL, data);

    FlImage id = fl_image_create_from_memory(ctx, "svg", data, size);
    ASSERT_NE(0, id);

    FlAllocator_free(state->global_allocator, data);

    fl_image_destroy(ctx, id);
    fl_context_destroy(ctx);
    fl_destroy(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Image, load_file_get_data_ok) {
    struct FlGlobalState* state = fl_create(NULL);
    struct FlContext* ctx = fl_context_create(state);

    FlImage id = fl_image_create_from_file(ctx, "data/uv.png");
    ASSERT_NE(0, id);

    FlImageInfo* data = fl_image_get_info(ctx, id);
    ASSERT_EQ(512, data->width);
    ASSERT_EQ(512, data->height);

    fl_image_destroy(ctx, id);
    fl_context_destroy(ctx);
    fl_destroy(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Image, load_file_get_invalid_data) {
    struct FlGlobalState* state = fl_create(NULL);
    struct FlContext* ctx = fl_context_create(state);

    FlImage id = fl_image_create_from_file(ctx, "data/uv.png");
    ASSERT_NE(0, id);

    fl_image_destroy(ctx, id);
    FlImageInfo* data = fl_image_get_info(ctx, id);
    ASSERT_EQ(NULL, data);

    fl_image_destroy(ctx, id);
    fl_context_destroy(ctx);
    fl_destroy(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Image, load_file_get_invalid_data_2) {
    struct FlGlobalState* state = fl_create(NULL);
    struct FlContext* ctx = fl_context_create(state);

    FlImage id = fl_image_create_from_file(ctx, "data/no_such_file.png");
    ASSERT_EQ(0, id);

    FlImageInfo* data = fl_image_get_info(ctx, id);
    ASSERT_EQ(NULL, data);

    fl_image_destroy(ctx, id);
    fl_context_destroy(ctx);
    fl_destroy(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Image, render_image) {
    struct FlGlobalState* state = fl_create(NULL);
    struct FlContext* ctx = fl_context_create(state);

    FlImage id = fl_image_create_from_file(ctx, "data/uv.png");
    ASSERT_NE(0, id);

    int count = fl_render_begin_commands(state);
    const u8* cmd_data = NULL;

    bool found_create_texture = false;
    bool found_update_texture = false;
    bool found_render_texture = false;

    // process all the render commands
    for (int i = 0; i < count; ++i) {
        switch (fl_render_get_command(state, &cmd_data)) {
            case FlRenderCommand_CreateTexture: {
                const FlCreateTexture* cmd = (FlCreateTexture*)cmd_data;
                // Don't assert directly as we may have more textures being created
                // so we verify that we have one that matches what we want
                if (cmd->width == 4096 && cmd->height == 4096 && cmd->format == FlTextureFormat_Rgba8Srgb) {
                    found_create_texture = true;
                }

                break;
            }
        }
    }

    ASSERT_TRUE(found_create_texture);

    fl_frame_begin(ctx, 640, 480, 1.0f / 60.0f);
    fl_ui_image(ctx, id);
    fl_frame_end(ctx);

    count = fl_render_begin_commands(state);

    // Expect a update texture command here
    for (int i = 0; i < count; ++i) {
        switch (fl_render_get_command(state, &cmd_data)) {
            case FlRenderCommand_UpdateTexture: {
                const FlUpdateTexture* cmd = (FlUpdateTexture*)cmd_data;
                ASSERT_NE(cmd->data, NULL);
                ASSERT_EQ(cmd->texture_id, state->images_atlas->texture_id);
                found_update_texture = true;
                break;
            }

            case FlRenderCommand_TexturedTriangles: {
                const FlTexturedTriangles* cmd = (FlTexturedTriangles*)cmd_data;
                ASSERT_EQ(4, cmd->vertex_buffer_size);
                ASSERT_EQ(6, cmd->index_buffer_size);
                ASSERT_EQ(cmd->texture_id, state->images_atlas->texture_id);
                found_render_texture = true;
                break;
            }
        }
    }

    // validate that we have created texture and rendering verts
    ASSERT_TRUE(found_update_texture);
    ASSERT_TRUE(found_render_texture);

    fl_image_destroy(ctx, id);
    fl_context_destroy(ctx);
    fl_destroy(state);
}

#endif
