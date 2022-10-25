///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Only for API prototyping/ideas/not functional yet

#include "flowi.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct AreaText {
    FlStyle* area_style;
    FlStyle* header_style;
    FlStyle* text_style;
} AreaText;

static AreaText s_area_text;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void* area_text_create(FlContext* ctx) {
    AreaText* user_data = s_area_text;

    FlStyle* area_style = fl_style_create(ctx, "area");
    FlStyle* header_style = fl_style_create(ctx, "header");
    FlStyle* text_style = fl_style_create(ctx, "text");

    // Area style
    area_style->border = (FlBorder) {
        .border_raidus {
            .value = 40.0f,
            .value = 40.0f,
            .value = 40.0f,
            .value = 40.0f,
        }
    };
    area_style->background_color = FL_RGB(0x19, 0xe, 0x23);

    // About text
    about_style->margin = {
        0, 0,
        40, 40,
    },
    header_style->font_size = 20;
    header_style->color = FL_RGB(0xe0, 0xe0, 0xe0);

    // Text style
    text_style->font_size = 14;
    text_style->color = FL_RGB(0xf0, 0xf0, 0xf0);

    return user_data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void area_text_update(void* user_data, FlContext* ctx) {
    AreaText* data = (AreaText*)user_data;

    fl_area_push_with_style(ctx, data->area_style);
    fl_text_with_style(ctx, "About Flowi", data->header_style);
    fl_text_with_style(ctx, "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam", data->text_style);
    fl_area_pop();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void area_text_destroy(void* user_data, FlContext* ctx) {
    AreaText* data = (AreaText*)user_data;
    fl_style_destroy(ctx, user_data->area_style);
    fl_style_destroy(ctx, user_data->header_style);
    fl_style_destroy(ctx, user_data->text_style);
}


