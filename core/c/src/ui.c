#include <flowi_core/math_data.h>
#include <flowi_core/ui.h>
#include "font_private.h"
#include "image_private.h"
#include "internal.h"
#include "text.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set position for the next ui-element (this is used when [LayoutMode::Manual] is used)

void fl_ui_set_pos_impl(struct FlContext* ctx, FlVec2 pos) {
    ctx->cursor = pos;
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

    // Add image for rendering
    {
        PrimitiveImage* prim = Primitive_alloc_image(ctx->global);
        prim->image = image_data;
        prim->position = pos;
        prim->size.x = image_size.x;
        prim->size.y = image_size.y;
    }

    // Add text for rendering
    {
        PrimitiveText* prim = Primitive_alloc_text(ctx->global);

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
