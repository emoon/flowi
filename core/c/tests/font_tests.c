#include <flowi_core/font.h>
#include "../src/atlas.h"
#include "../src/font_private.h"
#include "../src/internal.h"
#include "utest.h"

struct FlContext;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Font, load_failed) {
    struct FlGlobalState* state = fl_create(NULL);
    struct FlContext* ctx = fl_context_create(state);

    FlFont font_id = fl_font_new_from_file(ctx, "unable_to_load.bin", 12, FlFontPlacementMode_Auto);

    // Expect loading fail
    ASSERT_TRUE(font_id == 0);

    fl_context_destroy(ctx);
    fl_destroy(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Font, load_font_ok) {
    struct FlGlobalState* state = fl_create(NULL);
    struct FlContext* ctx = fl_context_create(state);

    FlFont font_id = fl_font_new_from_file(ctx, "data/montserrat-regular.ttf", 36, FlFontPlacementMode_Auto);

    // Expect loading to work
    ASSERT_NE(0, font_id);

    // fl_font_destroy(font_id);
    fl_context_destroy(ctx);
    fl_destroy(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Font, calc_text_size) {
    struct FlGlobalState* state = fl_create(NULL);
    struct FlContext* ctx = fl_context_create(state);

    FlFont font_id = fl_font_new_from_file(ctx, "data/montserrat-regular.ttf", 36, FlFontPlacementMode_Auto);

    u32 codepoints[] = {'A', 'B', 'c', ' '};

    ctx->current_font = (Font*)Handles_get_data(&state->font_handles, font_id);
    ctx->current_font_size = 36;

    FlIVec2 size = Font_calc_text_size(ctx, codepoints, 4);

    ASSERT_EQ(66, size.x);
    ASSERT_EQ(25, size.y);

    fl_context_destroy(ctx);
    fl_destroy(state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Font, gen_glyph_verify_render_cmds) {
    struct FlGlobalState* state = fl_create(NULL);
    struct FlContext* ctx = fl_context_create(state);

    FlFont font_id = fl_font_new_from_file(ctx, "data/montserrat-regular.ttf", 36, FlFontPlacementMode_Auto);
    u32 test[] = {64, 65};

    int count = fl_render_begin_commands(state);
    const u8* cmd_data = NULL;

    bool found_create_texture = false;
    bool found_update_texture = false;

    // process all the render commands
    for (int i = 0; i < count; ++i) {
        switch (fl_render_get_command(state, &cmd_data)) {
            case FlRenderCommand_CreateTexture: {
                const FlCreateTexture* cmd = (FlCreateTexture*)cmd_data;
                // Don't assert directly as we may have more textures being created
                // so we verify that we have one that matches what we want
                if (cmd->width == 4096 && cmd->height == 4096 && cmd->format == FlTextureFormat_R8Linear) {
                    found_create_texture = true;
                }

                break;
            }
        }
    }

    ASSERT_TRUE(found_create_texture);

    // Begin frame and generate some glyphs and figure out the range to update

    fl_frame_begin(ctx, 640, 480, 1.0f / 60.f);

    Atlas_begin_add_rects(state->mono_fonts_atlas);
    Font_generate_glyphs(ctx, (Font*)Handles_get_data(&state->font_handles, font_id), test, 2, 36);
    Atlas_end_add_rects(state->mono_fonts_atlas, state);

    fl_frame_end(ctx);

    count = fl_render_begin_commands(state);

    // Expect a update texture command here
    for (int i = 0; i < count; ++i) {
        switch (fl_render_get_command(state, &cmd_data)) {
            case FlRenderCommand_UpdateTexture: {
                const FlUpdateTexture* cmd = (FlUpdateTexture*)cmd_data;
                ASSERT_NE(cmd->data, NULL);
                ASSERT_EQ(cmd->texture_id, state->mono_fonts_atlas->texture_id);
                found_update_texture = true;
                break;
            }
        }
    }

    // validate that we have created some textures
    ASSERT_TRUE(found_update_texture);

    fl_font_destroy(ctx, font_id);
    fl_context_destroy(ctx);
    fl_destroy(state);
}
