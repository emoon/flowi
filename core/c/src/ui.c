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

bool fl_ui_push_button_with_icon_impl(FlContext* ctx, FlString text, FlImage image, FlPushButtonWithIconArgs args) {
    FL_UNUSED(ctx);
    FL_UNUSED(text);
    FL_UNUSED(image);
    FL_UNUSED(args);

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
    FlIVec2 image_size;
    // FlIVec2 total_size = {0};

    if (args.image_size.x != 0) {
        image_size = args.image_size;
    } else {
        image_size.x = image_data->info.width;
        image_size.y = image_data->info.height;
    }

    int text_offset = 0;
    int image_offset = 0;

    // TODO: This is incorrect with up -> down text

    // Center image
    if (image_size.y > text_size.y) {
        text_offset = (image_size.y - text_size.y) * 0.5f;
        // total_size.y = image_size.y;
    } else {
        image_offset = (text_size.y - image_size.y) * 0.5f;
        // total_size.y = text_size.y;
    }

    FL_UNUSED(text_offset);

    // TODO: Fix, remove hardcoded padding
    // total_size.x = image_size.x + (text_size.x + 10.0f);

    FlVec2 pos = ctx->cursor;

    // Add image for rendering
    {
        PrimitiveImage* prim = Primitive_alloc_image(ctx->global);
        prim->image = image_data;
        prim->position.x = pos.x;
        prim->position.x = pos.y + image_offset;
        prim->size.x = image_size.x;
        prim->size.y = image_size.y;
    }

    // Add text for rendering
    {
        PrimitiveText* prim = Primitive_alloc_text(ctx->global);

        prim->font = ctx->current_font;
        prim->position.x = pos.x + image_size.x + 10;
        prim->position.y = pos.y + text_size.y + text_offset;
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
