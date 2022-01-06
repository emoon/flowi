#include "utest.h"
#include "../src/font.h"
#include "../src/atlas.h"
#include "../src/font_private.h"
#include "../src/internal.h"

struct FlContext;

extern struct FlContext* g_ctx;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Font, load_failed) {
    FlFont font_id = fl_font_create_from_file(g_ctx, "unable_to_load.bin", 12, FlFontGlyphPlacementMode_Auto);

    // Expect loading fail
    ASSERT_TRUE(font_id == -1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Font, load_font_ok) {
    FlFont font_id = fl_font_create_from_file(g_ctx, "data/montserrat-regular.ttf", 36, FlFontGlyphPlacementMode_Auto);

    // Expect loading to work
    ASSERT_TRUE(font_id == 0);

    fl_font_destroy(g_ctx, font_id);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Font, gen_glyph_verify_render_cmds) {
    struct FlGlobalState* state = fl_create(NULL);
    struct FlContext* ctx = fl_context_create(state);

    FlFont font_id = fl_font_create_from_file(ctx, "data/montserrat-regular.ttf", 36, FlFontGlyphPlacementMode_Auto);
    u32 test[] = { 64, 65 };

	const FlRenderData* render_data = fl_get_render_data(ctx);
    const u8* render_commands = render_data->render_commands;
    const u8* render_cmd_data = render_data->render_data;
    bool found_create_texture = false;
    bool found_update_texture = false;

    // process all the render commands
    // TODO: We need to support skipping commands also
    for (int i = 0, count = render_data->count; i < count; ++i) {
        FlRenderCommand cmd = (FlRenderCommand)*render_commands++;

        switch (cmd) {
			case FlRc_CreateTexture: {
				const FlRcCreateTexture* cmd = (FlRcCreateTexture*)render_cmd_data;
				ASSERT_EQ(cmd->format, FlTextureFormat_R8_LINEAR);
				ASSERT_EQ(cmd->width, 4096);
				ASSERT_EQ(cmd->height, 4096);

				render_cmd_data = (u8*)(cmd + 1);
				count = render_data->count; // end loop
				found_create_texture = true;
				break;
			}

			default: {
				ASSERT_FALSE(true); // TODO: Must handle unsupported data
				break;
			}
		}

		if (found_create_texture)
			break;
	}

	ASSERT_TRUE(found_create_texture);

	// Begin frame and generate some glyphs and figure out the range to update

	fl_frame_begin(ctx);

	Atlas_begin_add_rects(state->mono_fonts_atlas);
	Font_generate_glyphs(ctx, font_id, test, 2, 36);
	Atlas_end_add_rects(state->mono_fonts_atlas, state);

	fl_frame_end(ctx);

	render_data = fl_get_render_data(ctx);
    render_commands = render_data->render_commands;
    render_cmd_data = render_data->render_data;

    // Expect a update texture command here

    for (int i = 0, count = render_data->count; i < count; ++i) {
        FlRenderCommand cmd = (FlRenderCommand)*render_commands++;

        switch (cmd) {
			case FlRc_UpdateTexture: {
				const FlRcUpdateTexture* cmd = (FlRcUpdateTexture*)render_cmd_data;
				ASSERT_EQ(cmd->texture_id, state->mono_fonts_atlas->texture_id);
				found_update_texture = true;
				break;
			}

			default: {
				ASSERT_FALSE(true); // TODO: Must handle unsupported data
				break;
			}
		}

		if (found_update_texture)
			break;
	}

	ASSERT_TRUE(found_update_texture);

	// validate that we have created some textures

	fl_destroy(state);
}



