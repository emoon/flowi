#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <flowi/ui.h>
#include <flowi/flowi.h>
#include <flowi/io.h>
#include <flowi/application.h>
#include "allocator.h"
#include "atlas.h"
#include "flowi_internal.h"
#include "font_private.h"
#include "image_private.h"
#include "internal.h"
#include "layout_private.h"
#include "primitive_rect.h"
#include "primitives.h"
#include <dear-imgui/imgui.h>
//#include "render.h"
#include "style_internal.h"
#include "text.h"
#include "vertex_allocator.h"

#if defined(_MSC_VER)
#include <malloc.h>
#undef aligned_alloc
#define aligned_alloc(align, size) _aligned_malloc(size, align)
#endif

// TODO: Block based allocations
#define MAX_CONTROLS 1024
#define WIDGET_SPACING 2
#define MAX_RENDER_COMMANDS 256
#define MAX_RENDER_COMMAND_DATA (1024 * 1024)

// Temp
#define MAX_VERTS 1024

// We allocate SIMD size extra memory so we can read "out of bounds"
#define MEMORY_PADDING 4

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Malloc based allocator. We should use tslf or similar in a sandbox, but this is atleast in one place

static void* alloc_malloc(void* user_data, u64 size) {
    FL_UNUSED(user_data);
    return malloc(size);
}

static void* realloc_malloc(void* user_data, void* ptr, u64 size) {
    FL_UNUSED(user_data);
    return realloc(ptr, size);
}

static void free_malloc(void* user_data, void* ptr) {
    FL_UNUSED(user_data);
    free(ptr);
}

static void memory_error(void* user_data, const char* text, int text_len) {
    FL_UNUSED(user_data);
    FL_UNUSED(text);
    FL_UNUSED(text_len);
    printf("Ran out of memory! :(\n");
}

static FlAllocator malloc_allocator = {
    FlAllocatorError_Exit, NULL, memory_error, alloc_malloc, NULL, realloc_malloc, free_malloc,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" FlContext* fl_context_create(struct FlGlobalState* state) {
    // TODO: Custom allocator
    FlContext*  ctx = FlAllocator_alloc_zero_type(&malloc_allocator, FlContext);
    FlInternalData* data = FlAllocator_alloc_zero_type(&malloc_allocator, FlInternalData);
    // TODO: Configure these values
    // int vertex_sizes[VertexAllocType_SIZEOF] = {1024 * 1024, 1024 * 1024};
    // int index_sizes[VertexAllocType_SIZEOF] = {512 * 1024, 512 * 1024};

    LinearAllocator_create_with_allocator(&data->frame_allocator, "string tracking allocator", &malloc_allocator,
                                          10 * 1024, true);

    // VertexAllocator_create(&ctx->vertex_allocator, &malloc_allocator, vertex_sizes, index_sizes, true);
    // LinearAllocator_create_with_allocator(&ctx->layout_allocator, "layout allocator", &malloc_allocator, 1024, true);
    StringAllocator_create(&data->string_allocator, &malloc_allocator, &data->frame_allocator);

    for (int i = 0; i < FlLayerType_Count; ++i) {
        CommandBuffer_create(&data->layers[i].primitive_commands, "primitives", state->global_allocator, 4 * 1024);
    }
    
    printf("create context\n");

    //*ctx = s_context;
    ctx->priv = data;

    /*
    data->button_funcs = g_button_funcs;
    data->cursor_funcs = g_cursor_funcs;
    data->font_funcs = g_font_funcs;
    data->image_funcs = g_image_funcs;
    data->io_funcs = g_io_funcs;
    data->item_funcs = g_item_funcs;
    data->menu_funcs = g_menu_funcs;
    data->text_funcs = g_text_funcs;
    data->ui_funcs = g_ui_funcs;
    data->style_funcs = g_style_funcs;
    data->window_funcs = g_window_funcs;

    data->button_funcs.priv = data;
    data->cursor_funcs.priv = data;
    data->font_funcs.priv = data;
    data->image_funcs.priv = data;
    data->io_funcs.priv = (FlInternalData*)data->io_handler;
    data->item_funcs.priv = data;
    data->menu_funcs.priv = data;
    data->text_funcs.priv = data;
    data->ui_funcs.priv = data;
    data->style_funcs.priv = data;
    data->window_funcs.priv = data;
    */

    // Layout_create_default(ctx);
    // ctx->layout_mode = FlLayoutMode_Automatic;

    // TODO: Fixuw
    data->global = state;

    /*
    if (hashmap_create(FL_MAX_WIDGET_IDS, &data->widget_states) != 0) {
        // TODO: Error
        return NULL;
    }
    */

    /*
    for (int i = 0; i < FlLayerType_Count; ++i) {
        CommandBuffer_create(&ctx->layers[i].primitive_commands, "primitives", state->global_allocator, 4 * 1024);
    }
    */

    return ctx;
}
//

void fl_style_init_priv();

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This to be called before using any other functions

extern "C" struct FlGlobalState* fl_create(const FlSettings* settings) {
    FL_UNUSED(settings);

    fl_style_init_priv();

    FlGlobalState* state = FlAllocator_alloc_zero_type(&malloc_allocator, FlGlobalState);
    state->global_allocator = &malloc_allocator;

    state->texture_ids = 1;

    CommandBuffer_create(&state->render_commands, "primitives", state->global_allocator, 4 * 1024);
    Handles_create(&state->image_handles, state->global_allocator, 16, ImagePrivate);

    // TODO: We should check settings for texture size
    state->images_atlas = Atlas_create(4096, 4096, AtlasImageType_RGBA8, state, state->global_allocator);
    state->font_atlas = new ImFontAtlas();

    return state;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" void fl_frame_begin(FlInternalData* ctx, int width, int height, float delta_time) {
    FL_UNUSED(width);
    FL_UNUSED(height);

    /*
    for (int i = 0; i < FlLayerType_Count; ++i) {
        CommandBuffer_rewind(&ctx->layers[i].primitive_commands);
    }
    */

    CommandBuffer_rewind(&ctx->string_allocator.commands);

    ctx->delta_time = delta_time;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
void draw_text(struct FlContext* ctx, const u8* cmd) {
    PrimitiveText* prim = (PrimitiveText*)cmd;

    Font* font = prim->font;

    if (!font) {
        ERROR_ADD(FlError_Font, "No font set, unable to draw_text: %s", "TODO: Name");
        return;
    }

    const int text_len = prim->codepoint_count;

    FlVertPosUvColor* vertices = NULL;
    FlIdxSize* indices = NULL;

    if (!VertexAllocator_alloc_pos_uv_color(&ctx->vertex_allocator, &vertices, &indices, text_len * 4, text_len * 6)) {
        // TODO: Better error handling
        assert(0);
    }

    Text_generate_vertex_buffer_ref(vertices, indices, font, prim->font_size, prim->codepoints, 0x0fffffff,
                                    prim->position, 0, text_len);

    FlTexturedTriangles* tri_data = Render_textured_triangles_cmd(ctx->global);

    tri_data->offset = ctx->vertex_allocator.index_offset;
    tri_data->vertex_buffer = vertices;
    tri_data->index_buffer = indices;
    tri_data->vertex_buffer_size = text_len * 4;
    tri_data->index_buffer_size = text_len * 6;
    tri_data->texture_id = ctx->global->mono_fonts_atlas->texture_id;  // TODO: Fix me
}
*/

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// void generate_glyphs(struct FlContext* ctx, const u8* cmd) {
//    PrimitiveText* prim = (PrimitiveText*)cmd;
//   Font_generate_glyphs(ctx, prim->font, prim->codepoints, prim->codepoint_count, prim->font_size);
//}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" void fl_frame_end(struct FlInternalData* data) {
#if 0
    FlGlobalState* state = data->global;

    // first do generation pass to build up all glyphs and other data
    Atlas_begin_add_rects(state->images_atlas);

    for (int l = 0; l < FlLayerType_Count; ++l) {
        Layer* layer = &data->layers[l];

        const u8* command_data = NULL;
        const int command_count = CommandBuffer_begin_read_commands(&layer->primitive_commands);

        for (int i = 0; i < command_count; ++i) {
            switch (CommandBuffer_read_next_cmd(&layer->primitive_commands, &command_data)) {
                case Primitive_DrawImage: {
                    Image_add_to_atlas(command_data, state->images_atlas);
                    break;
                }
            }
        }

        CommandBuffer_rewind(&layer->primitive_commands);
    }

    Atlas_end_add_rects(state->images_atlas, state);
#endif
    //
#if 0
    for (int l = 0; l < 1; ++l) {
        Layer* layer = &ctx->layers[l];

        const u8* command_data = NULL;
        int command_count = CommandBuffer_begin_read_commands(&layer->primitive_commands);

        // TODO: Function pointers instead of switch?
        for (int i = 0; i < command_count; ++i) {
            switch (CommandBuffer_read_next_cmd(&layer->primitive_commands, &command_data)) {
                case Primitive_DrawRect: {
                    PrimitiveRect_generate_render_data(ctx, (PrimitiveRect*)command_data);
                    break;
                }
            }
        }

        // TODO: Fix this hack
        if (l == 0) {
            VertsCounts counts = VertexAllocator_get_pos_color_counts(&ctx->vertex_allocator);
            FlSolidTriangles* tri_data = Render_solid_triangles_cmd(ctx->global);

            tri_data->offset = ctx->vertex_allocator.frame_index;
            tri_data->vertex_buffer = counts.vertex_data;
            tri_data->index_buffer = counts.index_data;

            tri_data->vertex_buffer_size = counts.vertex_count;
            tri_data->index_buffer_size = counts.index_count;
        }

        command_data = NULL;
        command_count = CommandBuffer_begin_read_commands(&layer->primitive_commands);

        // TODO: Function pointers instead of switch?
        for (int i = 0; i < command_count; ++i) {
            switch (CommandBuffer_read_next_cmd(&layer->primitive_commands, &command_data)) {
                case Primitive_DrawText: {
                    draw_text(ctx, command_data);
                    break;
                }

                case Primitive_DrawImage: {
                    Image_render(ctx, command_data);
                    break;
                }
            }
        }

        CommandBuffer_rewind(&layer->primitive_commands);
    }
#endif

    // VertexAllocator_end_frame(&ctx->vertex_allocator);
    LinearAllocator_rewind(&data->frame_allocator);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
void fl_ui_text_impl(struct FlContext* ctx, FlString text) {
    Utf8Result res = Utf8_to_codepoints_u32(&ctx->frame_allocator, (u8*)text.str, text.len);

    if (FL_UNLIKELY(res.error != FlError_None)) {
        // TODO: Proper error
        printf("String is mall-formed\n");
        return;
    }

    Layer* layer = ctx_get_active_layer(ctx);

    PrimitiveText* prim = Primitive_alloc_text(layer);

    prim->font = ctx->current_font;
    prim->position = ctx->cursor;
    prim->font_size = ctx->current_font_size != 0 ? ctx->current_font_size : ctx->current_font->default_size;
    prim->codepoints = res.codepoints;
    prim->codepoint_count = res.len;
    prim->position_index = 0;  // TODO: Fixme
}
*/

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" void fl_context_destroy(FlContext* self) {
    FlInternalData* data = self->priv;
    FlAllocator* allocator = data->global->global_allocator;

    /*
    for (int i = 0; i < self->style_count; ++i) {
        FlAllocator_free(allocator, self->styles[i]);
    }
    */

    for (int i = 0; i < FlLayerType_Count; ++i) {
        Layer* layer = &data->layers[i];
        CommandBuffer_destroy(&layer->primitive_commands);
    }

    /*
    LinearAllocator_destroy(&self->layout_allocator);
    VertexAllocator_destroy(&self->vertex_allocator);
    */
    StringAllocator_destroy(&data->string_allocator);
    LinearAllocator_destroy(&data->frame_allocator);

    //hashmap_destroy(&data->widget_states);

    FlAllocator_free(allocator, self);
    FlAllocator_free(allocator, data);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" void fl_destroy(FlGlobalState* self) {
    FlAllocator* allocator = self->global_allocator;

    CommandBuffer_destroy(&self->render_commands);
    Atlas_destroy(self->images_atlas);

    Handles_destroy(&self->image_handles);
    FlAllocator_free(allocator, self);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Returns the number of render commands. use fl_render_get_cmd to get each command

extern "C" int fl_render_begin_commands(FlGlobalState* state) {
    return CommandBuffer_begin_read_commands(&state->render_commands);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" u16 fl_render_get_command(FlGlobalState* state, const u8** data) {
    return CommandBuffer_read_next_cmd(&state->render_commands, data);
}

static char s_dummy_buffer[512];

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" FlString fl_error_last_error() {
    strcpy(s_dummy_buffer, "TODO: Correct error");
    FlString ret = fl_cstr_to_flstring(s_dummy_buffer);
    return ret;
}
