#include <assert.h>
#include <freetype/freetype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <flowi_core/ui.h>
#include "allocator.h"
#include "atlas.h"
#include "flowi.h"
#include "font_private.h"
#include "image_private.h"
#include "internal.h"
#include "layout_private.h"
#include "primitive_rect.h"
#include "primitives.h"
#include "render.h"
#include "simd.h"
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

typedef enum ButtonState {
    None,
    Fading,
    Hover,
} ButtonState;

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

FlContext* fl_context_create(struct FlGlobalState* state) {
    // TODO: Custom allocator
    FlContext* ctx = FlAllocator_alloc_zero_type(&malloc_allocator, FlContext);
    // TODO: Configure these values
    int vertex_sizes[VertexAllocType_SIZEOF] = {1024 * 1024, 1024 * 1024};
    int index_sizes[VertexAllocType_SIZEOF] = {512 * 1024, 512 * 1024};

    LinearAllocator_create_with_allocator(&ctx->frame_allocator, "string tracking allocator", &malloc_allocator,
                                          10 * 1024, true);

    VertexAllocator_create(&ctx->vertex_allocator, &malloc_allocator, vertex_sizes, index_sizes, true);
    LinearAllocator_create_with_allocator(&ctx->layout_allocator, "layout allocator", &malloc_allocator, 1024, true);
    StringAllocator_create(&ctx->string_allocator, &malloc_allocator, &ctx->frame_allocator);

    Layout_create_default(ctx);
    ctx->layout_mode = FlLayoutMode_Automatic;

    // TODO: Fixup
    ctx->global = state;

    ctx->mouse.pos = (FlVec2){-FLT_MAX, -FLT_MAX};
    ctx->mouse.pos_prev = (FlVec2){-FLT_MAX, -FLT_MAX};

    // TODO: Grab from settings
    // = 0.30f - Time for a double-click, in seconds.
    ctx->mouse.double_click_time = 0.30f;
    // = 6.0f - Distance threshold to stay in to validate a double-click, in pixels.
    ctx->mouse.double_click_max_dist = 6.0f;

    for (int i = 0; i < MAX_MOUSE_BUTTONS; i++) {
        ctx->mouse.down_duration[i] = ctx->mouse.down_duration_prev[i] = -1.0f;
    }

    if (hashmap_create(FL_MAX_WIDGET_IDS, &ctx->widget_states) != 0) {
        // TODO: Error
        return NULL;
    }

    for (int i = 0; i < FlLayerType_Count; ++i) {
        CommandBuffer_create(&ctx->layers[i].primitive_commands, "primitives", state->global_allocator, 4 * 1024);
    }

    return ctx;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This to be called before using any other functions

struct FlGlobalState* fl_create(const FlSettings* settings) {
    FL_UNUSED(settings);

    FlGlobalState* state = FlAllocator_alloc_zero_type(&malloc_allocator, FlGlobalState);
    state->global_allocator = &malloc_allocator;

    CommandBuffer_create(&state->render_commands, "primitives", state->global_allocator, 4 * 1024);
    Handles_create(&state->image_handles, state->global_allocator, 16, ImagePrivate);
    Handles_create(&state->font_handles, state->global_allocator, 16, Font);

    // TODO: We should check settings for texture size
    state->mono_fonts_atlas = Atlas_create(4096, 4096, AtlasImageType_U8, state, state->global_allocator);

    // TODO: We should check settings for texture size
    state->images_atlas = Atlas_create(4096, 4096, AtlasImageType_RGBA8, state, state->global_allocator);

    Font_init(state);

    return state;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void set_active_id(FlContext* ctx, FlowiID id) {
    ctx->active_id_is_just_activated = ctx->active_id != id;

    if (ctx->active_id_is_just_activated) {
        ctx->active_id_timer = 0.0f;
        ctx->active_id_has_been_pressed_before = false;
        ctx->active_id_has_been_edited_before = false;
        if (id != 0) {
            ctx->last_active_id = id;
            ctx->last_active_id_timer = 0.0f;
        }
    }

    ctx->active_id = id;
    ctx->active_id_allow_overlap = false;
    ctx->active_id_no_clear_on_focus_loss = false;
    ctx->active_id_has_been_edited_this_frame = false;

    if (id) {
        ctx->active_id_is_alive = id;
        /*
        ctx->active_id_source =
            (ctx->nav_activate_id == id ||
             ctx->nav_input_id == id ||
             ctx->nav_just_tabbed_id == id ||
             ctx->nav_just_moved_to_id  == id) ? InputSource_Nav : InputSource_Mouse;
        */
    }

    // ctx->active_id_window = window;
    // Clear declaration of inputs claimed by the widget
    // (Please note that this is WIP and not all keys/inputs are thoroughly declared by all widgets yet)
    // ctx->active_id_using_mouse_wheel = false;
    // ctx->active_id_using_nav_dir_mask = 0x00;
    // ctx->active_id_using_nav_input_mask = 0x00;
    // ctx->active_id_using_key_input_mask = 0x00;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void clear_active_id(FlContext* ctx) {
    set_active_id(ctx, 0);
}

// clang-format off
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Borrows some ideas/code for Dear Imgui - MIT License (MIT) Copyright (c) 2014-2022 Omar Cornut
//
// The button_behavior() function is key to many interactions and used by many/most widgets.
// Because we handle so many cases (keyboard/gamepad navigation, drag and drop) and many specific behavior (via ButtonFlags_),
// this code is a little complex.
// By far the most common path is interacting with the Mouse using the default ButtonFlags_PressedOnClickRelease button behavior.
// See the series of events below and the corresponding state reported by dear imgui:
//------------------------------------------------------------------------------------------------------------------------------------------------
// with PressedOnClickRelease:             return-value  IsItemHovered()  IsItemActive()  IsItemActivated()  IsItemDeactivated()  IsItemClicked()
//   Frame N+0 (mouse is outside bb)        -             -                -               -                  -                    -
//   Frame N+1 (mouse moves inside bb)      -             true             -               -                  -                    -
//   Frame N+2 (mouse button is down)       -             true             true            true               -                    true
//   Frame N+3 (mouse button is down)       -             true             true            -                  -                    -
//   Frame N+4 (mouse moves outside bb)     -             -                true            -                  -                    -
//   Frame N+5 (mouse moves inside bb)      -             true             true            -                  -                    -
//   Frame N+6 (mouse button is released)   true          true             -               -                  true                 -
//   Frame N+7 (mouse button is released)   -             true             -               -                  -                    -
//   Frame N+8 (mouse moves outside bb)     -             -                -               -                  -                    -
//------------------------------------------------------------------------------------------------------------------------------------------------
// with PressedOnClick:                    return-value  IsItemHovered()  IsItemActive()  IsItemActivated()  IsItemDeactivated()  IsItemClicked()
//   Frame N+2 (mouse button is down)       true          true             true            true               -                    true
//   Frame N+3 (mouse button is down)       -             true             true            -                  -                    -
//   Frame N+6 (mouse button is released)   -             true             -               -                  true                 -
//   Frame N+7 (mouse button is released)   -             true             -               -                  -                    -
//------------------------------------------------------------------------------------------------------------------------------------------------
// with PressedOnRelease:                  return-value  IsItemHovered()  IsItemActive()  IsItemActivated()  IsItemDeactivated()  IsItemClicked()
//   Frame N+2 (mouse button is down)       -             true             -               -                  -                    true
//   Frame N+3 (mouse button is down)       -             true             -               -                  -                    -
//   Frame N+6 (mouse button is released)   true          true             -               -                  -                    -
//   Frame N+7 (mouse button is released)   -             true             -               -                  -                    -
//------------------------------------------------------------------------------------------------------------------------------------------------
// with PressedOnDoubleClick:              return-value  IsItemHovered()  IsItemActive()  IsItemActivated()  IsItemDeactivated()  IsItemClicked()
//   Frame N+0 (mouse button is down)       -             true             -               -                  -                    true
//   Frame N+1 (mouse button is down)       -             true             -               -                  -                    -
//   Frame N+2 (mouse button is released)   -             true             -               -                  -                    -
//   Frame N+3 (mouse button is released)   -             true             -               -                  -                    -
//   Frame N+4 (mouse button is down)       true          true             true            true               -                    true
//   Frame N+5 (mouse button is down)       -             true             true            -                  -                    -
//   Frame N+6 (mouse button is released)   -             true             -               -                  true                 -
//   Frame N+7 (mouse button is released)   -             true             -               -                  -                    -
//------------------------------------------------------------------------------------------------------------------------------------------------
// Note that some combinations are supported,
// - PressedOnDragDropHold can generally be associated with any flag.
// - PressedOnDoubleClick can be associated by PressedOnClickRelease/PressedOnRelease, in which case the second release event won't be reported.
//------------------------------------------------------------------------------------------------------------------------------------------------
// The behavior of the return-value changes when ButtonFlags_Repeat is set:
//                                         Repeat+                  Repeat+           Repeat+             Repeat+
//                                         PressedOnClickRelease    PressedOnClick    PressedOnRelease    PressedOnDoubleClick
//-------------------------------------------------------------------------------------------------------------------------------------------------
//   Frame N+0 (mouse button is down)       -                        true              -                   true
//   ...                                    -                        -                 -                   -
//   Frame N + RepeatDelay                  true                     true              -                   true
//   ...                                    -                        -                 -                   -
//   Frame N + RepeatDelay + RepeatRate*N   true                     true              -                   true
//-------------------------------------------------------------------------------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// clang-format on

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool is_mouse_pos_valid(FlVec2 pos) {
    const float MOUSE_INVALID = -256000.0f;
    return pos.x >= MOUSE_INVALID && pos.y >= MOUSE_INVALID;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void update_mouse_states(FlContext* ctx) {
    MouseState* state = &ctx->mouse;
    const double time = ctx->time;
    const float delta_time = ctx->delta_time;
    const float double_click_time = state->double_click_time;
    const float double_click_max_dist_sqr = state->double_click_max_dist * state->double_click_max_dist;
    bool valid_mouse_pos = false;

    FlVec2 zero = vec2_zero();

    // Round mouse position to avoid spreading non-rounded position
    if (is_mouse_pos_valid(state->pos)) {
        state->pos = vec2_floor(state->pos);
        valid_mouse_pos = true;
    }

    // If mouse just appeared or disappeared (usually denoted by -FLT_MAX components) we cancel out movement in
    // MouseDelta
    if (is_mouse_pos_valid(state->pos) && is_mouse_pos_valid(state->pos_prev)) {
        state->delta = vec2_sub(state->pos, state->pos_prev);
    } else {
        state->delta = zero;
    }

    if (state->delta.x != 0.0f || state->delta.y != 0.0f) {
        state->nav_disable_hover = false;
    }

    const FlVec2 pos = state->pos;
    state->pos_prev = pos;

    for (int i = 0; i < MAX_MOUSE_BUTTONS; i++) {
        const float down_dur = state->down_duration[i];
        const bool is_down = state->down[i];

        state->clicked[i] = is_down && (down_dur < 0.0f);
        state->released[i] = !is_down && (down_dur >= 0.0f);
        state->down_duration_prev[i] = down_dur;
        state->down_duration[i] = is_down ? (down_dur < 0.0f ? 0.0f : down_dur + delta_time) : -1.0f;
        state->double_clicked[i] = false;

        /*
        if (i == 0) {
            printf("is_down: %d - clicked %d %d\n", is_down, state->clicked[i], down_dur < 0.0f);
            printf("%f %f %f\n", state->down_duration[i], state->down_duration_prev[i], delta_time);
        }
        */

        if (state->clicked[i]) {
            if ((float)(time - state->clicked_time[i]) < double_click_time) {
                FlVec2 delta_click_pos = valid_mouse_pos ? (vec2_sub(pos, state->clicked_pos[i])) : zero;

                if (vec2_length_sqr(delta_click_pos) < double_click_max_dist_sqr) {
                    state->double_clicked[i] = true;
                }

                // Mark as "old enough" so the third click isn't turned into a double-click
                state->clicked_time[i] = -double_click_time * 2.0f;
            } else {
                state->clicked_time[i] = time;
            }

            state->clicked_pos[i] = state->pos;
            state->down_was_double_click[i] = state->double_clicked[i];
            state->drag_max_distance_abs[i] = zero;
            state->drag_max_distance_sqr[i] = 0.0f;
        } else if (state->down[i]) {
            // Maintain the maximum dist we reaching from the initial click pos, which is used with dragging threshold
            const FlVec2 delta_click_pos = valid_mouse_pos ? vec2_sub(pos, state->clicked_pos[i]) : zero;
            const float delta_len_sqr = vec2_length_sqr(delta_click_pos);
            state->drag_max_distance_sqr[i] = f32_max(state->drag_max_distance_sqr[i], delta_len_sqr);
            state->drag_max_distance_abs[i].x = f32_max(state->drag_max_distance_abs[i].x, f32_abs(delta_click_pos.x));
            state->drag_max_distance_abs[i].y = f32_max(state->drag_max_distance_abs[i].y, f32_abs(delta_click_pos.y));
        }

        if (!state->down[i] && !state->released[i]) {
            state->down_was_double_click[i] = false;
        }

        // Clicking any button reactivate mouse hovering which may have been deactivated by gamepad/keyboard navigation
        if (state->clicked[i]) {
            state->nav_disable_hover = false;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_set_mouse_pos_state(struct FlContext* ctx, FlVec2 pos, bool b1, bool b2, bool b3) {
    // Tracks the mouse state for this frame
    ctx->mouse.pos = pos;
    ctx->mouse.down[0] = b1;
    ctx->mouse.down[1] = b2;
    ctx->mouse.down[2] = b3;

    update_mouse_states(ctx);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// update hovered_id data

static void update_hover_active_id(FlContext* ctx, float delta_time) {
    if (!ctx->hovered_id_previous_frame) {
        ctx->hovered_id_timer = 0.0f;
    }

    if (!ctx->hovered_id_previous_frame || (ctx->hovered_id && ctx->active_id == ctx->hovered_id)) {
        ctx->hovered_id_not_active_timer = 0.0f;
    }

    if (ctx->hovered_id) {
        ctx->hovered_id_timer += delta_time;
    }

    if (ctx->hovered_id && ctx->active_id != ctx->hovered_id) {
        ctx->hovered_id_not_active_timer += delta_time;
    }

    ctx->hovered_id_previous_frame = ctx->hovered_id;
    ctx->hovered_id_previous_frame_using_mouse_wheel = ctx->hovered_id_using_mouse_wheel;
    ctx->hovered_id = 0;
    ctx->hovered_id_allow_overlap = false;
    ctx->hovered_id_using_mouse_wheel = false;
    ctx->hovered_id_disabled = false;

    // update active_id data (clear reference to active widget if the widget isn't alive anymore)
    if (ctx->active_id_is_alive != ctx->active_id && ctx->active_id_previous_frame == ctx->active_id &&
        ctx->active_id != 0) {
        clear_active_id(ctx);
    }

    if (ctx->active_id) {
        ctx->active_id_timer += delta_time;
    }

    ctx->last_active_id_timer += delta_time;
    ctx->active_id_previous_frame = ctx->active_id;
    // ctx->active_id_previous_frame_window = ctx->active_id_window;
    ctx->active_id_previous_frame_has_been_edited_before = ctx->active_id_has_been_edited_before;
    ctx->active_id_is_alive = 0;
    ctx->active_id_has_been_edited_this_frame = false;
    ctx->active_id_previous_frame_is_alive = false;
    ctx->active_id_is_just_activated = false;

    if (ctx->temp_input_id != 0 && ctx->active_id != ctx->temp_input_id) {
        ctx->temp_input_id = 0;
    }

    if (ctx->active_id == 0) {
        ctx->active_id_using_nav_dir_mask = 0x00;
        ctx->active_id_using_nav_input_mask = 0x00;
        ctx->active_id_using_key_input_mask = 0x00;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool hack_first_frame = false;

void fl_frame_begin(struct FlContext* ctx, int width, int height, float delta_time) {
    FL_UNUSED(width);
    FL_UNUSED(height);

    if (hack_first_frame) {
        CommandBuffer_rewind(&ctx->global->render_commands);
    }

    update_hover_active_id(ctx, delta_time);

    // Update default layout
    // FlRect rect = { 0, 0, width, height };
    // Layout_resolve(ctx, ctx->default_layout, &rect);

    hack_first_frame = true;

    for (int i = 0; i < FlLayerType_Count; ++i) {
        CommandBuffer_rewind(&ctx->layers[i].primitive_commands);
    }

    ctx->active_layout = ctx->active_layout;
    ctx->frame_count++;
    ctx->delta_time = delta_time;
    ctx->time += delta_time;

    ctx->cursor.x = 0;
    ctx->cursor.y = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void generate_glyphs(struct FlContext* ctx, const u8* cmd) {
    PrimitiveText* prim = (PrimitiveText*)cmd;
    Font_generate_glyphs(ctx, prim->font, prim->codepoints, prim->codepoint_count, prim->font_size);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_frame_end(struct FlContext* ctx) {
    FlGlobalState* state = ctx->global;

    // first do generation pass to build up all glyphs and other data
    Atlas_begin_add_rects(state->images_atlas);
    Atlas_begin_add_rects(state->mono_fonts_atlas);

    for (int l = 0; l < FlLayerType_Count; ++l) {
        Layer* layer = &ctx->layers[l];

        const u8* command_data = NULL;
        const int command_count = CommandBuffer_begin_read_commands(&layer->primitive_commands);

        for (int i = 0; i < command_count; ++i) {
            switch (CommandBuffer_read_next_cmd(&layer->primitive_commands, &command_data)) {
                case Primitive_DrawText: {
                    generate_glyphs(ctx, command_data);
                    break;
                }

                case Primitive_DrawImage: {
                    Image_add_to_atlas(command_data, state->images_atlas);
                    break;
                }
            }
        }
    }

    Atlas_end_add_rects(state->images_atlas, state);
    Atlas_end_add_rects(state->mono_fonts_atlas, state);

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

    VertexAllocator_end_frame(&ctx->vertex_allocator);
    LinearAllocator_rewind(&ctx->frame_allocator);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_context_destroy(struct FlContext* self) {
    FlAllocator* allocator = self->global->global_allocator;

    for (int i = 0; i < self->style_count; ++i) {
        FlAllocator_free(allocator, self->styles[i]);
    }

    for (int i = 0; i < FlLayerType_Count; ++i) {
        Layer* layer = &self->layers[i];
        CommandBuffer_destroy(&layer->primitive_commands);
    }

    LinearAllocator_destroy(&self->layout_allocator);
    VertexAllocator_destroy(&self->vertex_allocator);
    StringAllocator_destroy(&self->string_allocator);
    LinearAllocator_destroy(&self->frame_allocator);

    hashmap_destroy(&self->widget_states);

    FlAllocator_free(allocator, self);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_destroy(FlGlobalState* self) {
    FlAllocator* allocator = self->global_allocator;

    CommandBuffer_destroy(&self->render_commands);
    Atlas_destroy(self->mono_fonts_atlas);
    Atlas_destroy(self->images_atlas);

    Font* fonts = (Font*)self->font_handles.objects;

    for (int i = 0; i < self->font_handles.len; ++i) {
        Font_destroy(self, &fonts[i]);
    }

    Handles_destroy(&self->image_handles);
    Handles_destroy(&self->font_handles);

    FT_Done_FreeType(self->ft_library);
    FlAllocator_free(allocator, self);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Returns the number of render commands. use fl_render_get_cmd to get each command

int fl_render_begin_commands(FlGlobalState* state) {
    return CommandBuffer_begin_read_commands(&state->render_commands);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u16 fl_render_get_command(FlGlobalState* state, const u8** data) {
    return CommandBuffer_read_next_cmd(&state->render_commands, data);
}

static char s_dummy_buffer[512];

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_error_last_error() {
    strcpy(s_dummy_buffer, "TODO: Correct error");
    FlString ret = fl_cstr_to_flstring(s_dummy_buffer);
    return ret;
}
