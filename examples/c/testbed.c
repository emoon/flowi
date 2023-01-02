// TODO: flowi/font.h should just include tho core version
#include <flowi/flowi.h>
#include <flowi/application.h>
#include <stdio.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct App {
    //FlFont font_bold;
    //FlFont font;
    FlImage image;
    FlImage image2;
} App;

/*
#define FlPushButtonWithIconArgs_default2                                                          \
    (FlPushButtonWithIconArgs) {                                                                   \
        .image_alignment = FlImageAlignment_Left, .image_text_spacing = 0, .image_scale = { 1.0f } \
    }
*/

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void main_loop(struct FlContext* ctx, void* user_data) {
    App* app = (App*)user_data;

    FlUiApi* ui = fl_ui_api(ctx);
    FlWindowApi* window_api = fl_window_api(ctx);

    //fl_style_push_color(ctx, FlStyleColor_TitleBg, FlColor_new_rgb(1.0f, 0.0f, 0.0f));
    fl_window_begin(window_api, "Test®", FlWindowFlags_None);
    fl_ui_image(ui, app->image);
    fl_ui_image(ui, app->image2);
    fl_window_end(window_api);
    //fl_style_pop_color(ctx);


    /*

    FlVec2 pos = {40.0f, 0.0f};
    FlVec2 pos2 = {40.0f, 80.0f};
    // FlVec2 pos2 = {40.0f, 180.0f};
    // FlVec2 pos3 = {0.0f, 0.0f};
    //  const char* utf8_text = "®";

    fl_font_set_impl(ctx, app->font_bold);
    fl_font_set_with_size(ctx, 64);
    fl_ui_set_pos(ctx, pos);

    if (fl_ui_push_button(ctx, "Push me")) {
        printf("Pushed\n");
    }

    fl_ui_set_pos(ctx, pos2);

    if (fl_ui_push_button(ctx, "Push me 2")) {
        printf("Pushed 2\n");
    }
    */

    /*
    fl_ui_set_pos_ctx(ctx, (FlVec2){.x = 140.0f, .y = 80.0f});
    fl_ui_text(utf8_text);
    */

    // fl_font_set(ctx, app->font_bold);
    // fl_font_set_with_size(ctx, 64);
    //  fl_ui_push_button_with_icon(ctx, "HippoMusic", app->image, FlPushButtonWithIconArgs_default2);

    /*
    fl_ui_set_pos(pos2);
    fl_font_set(app->font);
    fl_font_set_with_size(32);
    fl_ui_text("Hola");
    */

    // fl_ui_set_pos(ctx, pos3);
    // fl_ui_image(ctx, app->image);
}



///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int main() {
    struct FlContext* ctx = NULL;

    if (!(ctx = fl_application_create("Test", "Test"))) {
        printf("Failed to open application!\n");
        return 0;
    }

    FlImageApi* image_api = fl_image_api(ctx);

    App app = {
        //.font_bold = fl_font_new_from_file(ctx, "data/Montserrat-Bold.ttf", 64, FlFontPlacementMode_Auto),
        //.font = fl_font_new_from_file(ctx, "data/montserrat-regular.ttf", 64, FlFontPlacementMode_Auto),
        // .image = fl_image_create_from_file(ctx, "data/recommendations.svg"),
        .image = fl_image_create_from_file(image_api, "/home/emoon/code/projects/rust_minifb/resources/uv.png"),
        //.image2 = fl_image_create_from_file(ctx, "/home/emoon/code/projects/rust_minifb/resources/planet.png"),
        .image2 = fl_image_create_svg_from_file(image_api, "data/recommendations.svg", 512, FlSvgFlags_Alpha),
    };

    fl_application_main_loop(main_loop, &app);

    return 0;
}
