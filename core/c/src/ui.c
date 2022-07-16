#include <flowi_core/math_data.h>
#include <flowi_core/style.h>
#include <flowi_core/ui.h>
#include "color.h"
#include "font_private.h"
#include "image_private.h"
#include "internal.h"
#include "style_internal.h"
#include "text.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool is_mouse_hovering_rect(const FlContext* ctx, FlRect rect) {
    // TODO: Math function
    // TODO: SIMD
    FlVec2 pos = ctx->mouse.pos;

    if (pos.x >= rect.x && pos.y >= rect.y) {
        FlVec2 pos2 = (FlVec2){rect.x + rect.width, rect.y + rect.height};

        if (pos.x < pos2.x && pos.y < pos2.y) {
            return true;
        }
    }

    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool item_hoverable(FlContext* ctx, FlRect rect, FlowiID id) {
    if (ctx->hovered_id != 0 && ctx->hovered_id != id) {
        return false;
    }

    if (ctx->active_id != 0 && ctx->active_id != id) {
        return false;
    }

    if (!is_mouse_hovering_rect(ctx, rect)) {
        return false;
    }

    return true;
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


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// taken from:
// http://blade.nagaokaut.ac.jp/cgi-bin/scat.rb/ruby/ruby-talk/142054
//
// djb  :: 99.8601 percent coverage (60 collisions out of 42884)
// elf  :: 99.5430 percent coverage (196 collisions out of 42884)
// sdbm :: 100.0000 percent coverage (0 collisions out of 42884) (this is the algo used)
// ...

static u32 str_hash(const char* string, int len) {
    u32 hash = 0;

    const u8* str = (const u8*)string;

    for (int i = 0; i < len; ++i) {
        u32 c = *str++;
        hash = c + (hash << 6) + (hash << 16) - hash;
    }

    return hash & 0x7FFFFFFF;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set position for the next ui-element (this is used when [LayoutMode::Manual] is used)

void fl_ui_set_pos_impl(struct FlContext* ctx, FlVec2 pos) {
    ctx->cursor = pos;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void keep_id_alive(struct FlContext* ctx, FlowiID id) {
    if (ctx->active_id == id) {
        ctx->active_id_is_alive = true;
    }

    if (ctx->active_id_previous_frame == id) {
        ctx->active_id_previous_frame_is_alive = true;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool item_add(struct FlContext* ctx, FlowiID id, FlRect rect) {
    ctx->last_item_data.id = id;
    ctx->last_item_data.rect = rect;
    ctx->last_item_data.status_flags = 0;

    if (id != 0) {
        keep_id_alive(ctx, id);
    }

    return is_mouse_hovering_rect(ctx, rect);
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_ui_push_button_with_icon_impl(struct FlContext* ctx, FlString text, FlImage image, FlVec2 text_pos,
                                      float image_scale) {
    Utf8Result utf8_res = Utf8_to_codepoints_u32(&ctx->frame_allocator, (u8*)text.str, text.len);

    if (utf8_res.error == FlError_Utf8Malformed) {
        // TODO: Proper error
        printf("illegal string\n");
        return false;
    }

    ImagePrivate* image_data = (ImagePrivate*)Handles_get_data(&ctx->global->image_handles, image);
    FL_UNUSED(image_data);

    if (!image) {
        // printf("Illegal image id %lx\n", (u64)image);
        return false;
    }

    // Calculate the size of the text
    FlIVec2 text_size = Font_calc_text_size(ctx, utf8_res.codepoints, utf8_res.len);

    FlIVec2 image_size = {
        (int)(image_data->info.width * image_scale),
        (int)(image_data->info.height * image_scale),
    };

    // adjust the text a bit to center with image
    int text_offset = (image_size.y - text_size.y);  // * 0.5f;

    FlVec2 pos = ctx->cursor;

    Layer* layer = ctx_get_active_layer(ctx);

    // Add image for rendering
    {
        PrimitiveImage* prim = Primitive_alloc_image(layer);
        prim->image = image_data;
        prim->position = pos;
        prim->size.x = image_size.x;
        prim->size.y = image_size.y;
    }

    // Add text for rendering
    {
        PrimitiveText* prim = Primitive_alloc_text(layer);

        prim->font = ctx->current_font;
        prim->position.x = pos.x + text_pos.x;
        prim->position.y = pos.y + text_pos.y + text_offset;
        prim->font_size = ctx->current_font_size != 0 ? ctx->current_font_size : ctx->current_font->default_size;
        prim->codepoints = utf8_res.codepoints;
        prim->codepoint_count = utf8_res.len;
        prim->position_index = 0;  // TODO: Fixme
    }

    // TODO: Include style in position calculation

    // Based on the current the the position where we should place the image and the text
    // FlVec2 pos = Style_get_position(ctx);

    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set the active layer for rendering

void fl_ui_set_layer_impl(struct FlContext* ctx, FlLayerType layer) {
    ctx->active_layer = layer;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_ui_window_begin_impl(struct FlContext* ctx, FlString name, uint32_t flags) {
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// End call for various types such as windows, lists, etc.

void fl_ui_end_impl(struct FlContext* ctx) {
}
