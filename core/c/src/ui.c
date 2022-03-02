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
        printf("Illegal image id %lx\n", image);
        return false;
    }

    // FlVec2 size = Font_calc_text_size(ctx, utf8_res.codepoints, utf8_res.len);

    return false;
    /*
    // Calculate the size of the text
    FlVec2 text_size = Text_calc_size(ctx, text);
    FlVec2 image_size = Image_get_size(ctx, image);
    FlVec2 total_size = {0};

    float text_offset = 0.0f;
    float image_offset = 0.0f;

    // TODO: This is incorrect with up -> down text

    // Center image
    if (image_size.y > text_size.y) {
        text_offset = (image_size.y - text_size.y) * 0.5f;
        total_size.y = image_size.y;
    } else {
        image_offset = (text_size.y - image_size.y) * 0.5f;
        total_size.y = text_size.y;
    }

    // TODO: Fix, remove hardcoded padding
    total_size.x = image_size.x + (text_size.x + 10.0f);

    // Based on the current the the position where we should place the image and the text
    FlVec2 pos = Style_get_position(ctx);
    */
}
