// examples/shared_smoke - headless end-to-end proof that a sample app links and
// runs against the shared library libflowi.so.
//
// It uses only the curated exported surface: fl_init (core bootstrap), the
// fl_app_* runner, and the fl_layout_* calls the ui_layout_area scope macro
// expands to. Not a windowed demo or an automated test - run it manually from the
// build tree. Automated shared-library smoke assertions are UTEST cases in
// unit_tests.

#include "app/app.h"
#include <core/core.h>
#include "ui/color.h"
#include "ui/layout.h"

#include <stdio.h>

static void frame(void* user_data) {
    (void)user_data;

    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }, .color = ui_color(24, 24, 32, 255),
                   .child_alignment = ui_align(Center, Center)) {
        ui_layout_area(.sizing = { .width = sizing_fixed(240), .height = sizing_fixed(160) },
                       .color = ui_color(200, 40, 50, 255)) {}
    }
}

int main(void) {
    fl_init(1); // one job worker thread for async image decode

    FlAppSettings settings = {
        .window_title = FL_S("flowi shared smoke"),
        .width = 320,
        .height = 200,
        .headless = true,
    };

    FlApp* app = fl_app_create(&settings);
    if (!app) {
        fprintf(stderr, "shared_smoke: fl_app_create(headless) failed\n");
        return 1;
    }

    fl_app_tick(app, NULL, frame);

    FlPixelReadback pixels = { 0 };
    if (!fl_app_read_pixels(app, &pixels) || pixels.pixels == NULL || pixels.width == 0 || pixels.height == 0) {
        fprintf(stderr, "shared_smoke: fl_app_read_pixels returned no surface\n");
        fl_app_destroy(app);
        return 1;
    }

    printf("shared_smoke: ok — %ux%u readback via libflowi.so\n", pixels.width, pixels.height);
    fl_app_destroy(app);
    return 0;
}
