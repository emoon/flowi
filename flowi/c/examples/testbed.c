// TODO: flowi/font.h should just include tho core version
#include <flowi/application.h>
#include <flowi_core/font.h>
#include <flowi_core/ui.h>
#include <stdio.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct App {
    FlFont font_bold;
    FlFont font;
    FlImage image;
} App;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void main_loop(struct FlContext* flowi_ctx, void* user_data) {
    App* app = (App*)user_data;

    FlVec2 pos = {40.0f, 80.0f};
    FlVec2 pos2 = {40.0f, 180.0f};
    FlVec2 pos3 = {0.0f, 0.0f};
    const char* utf8_text = "®";

    fl_ui_set_pos_ctx(flowi_ctx, (FlVec2){.x = 140.0f, .y = 80.0f});
    fl_font_set(app->font_bold);
    fl_font_set_with_size(64);
    fl_ui_text(utf8_text);

    /*
    fl_ui_set_pos(pos2);
    fl_font_set(app->font);
    fl_font_set_with_size(32);
    fl_ui_text("Hola");
    */

    fl_ui_set_pos(pos3);
    fl_ui_image(app->image);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int main() {
    struct FlContext* flowi_ctx = NULL;

    if (!(flowi_ctx = fl_application_create("Test", "Test"))) {
        printf("Failed to open application!\n");
        return 0;
    }

    App app = {
        .font_bold = fl_font_new_from_file("data/Montserrat-Bold.ttf", 64, FlFontPlacementMode_Auto),
        .font = fl_font_new_from_file("data/montserrat-regular.ttf", 64, FlFontPlacementMode_Auto),
        .image = fl_image_create_from_file("data/recommendations.svg"),
    };

    fl_application_main_loop(main_loop, &app);

    return 0;
}
