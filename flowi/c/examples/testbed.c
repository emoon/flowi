#include <flowi/application.h>
// TODO: flowi/font.h should just include tho core version
#include <flowi_core/font.h>
#include <flowi_core/ui.h>
#include <stdio.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct App {
	FlFont font;
} App;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void main_loop(struct FlContext* flowi_ctx, void* user_data) {
	App* app = (App*)user_data;

	fl_font_set(app->font);
	fl_ui_text("Test");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int main() {
	struct FlContext* flowi_ctx = NULL;

	if (!(flowi_ctx = fl_application_new("Test", "Test"))) {
		printf("Failed to open application!\n");
		return 0;
	}

	App app = {
    	.font = fl_font_new_from_file("data/montserrat-regular.ttf", 64, FlFontPlacementMode_Auto),
	};

	fl_application_main_loop(main_loop, &app);

	return 0;
}
