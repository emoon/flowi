// examples/app_smoke - the canonical direct-C usage of the fl_app runner.
//
// A standalone SDL window driven entirely through the fl_app C ABI: fl_app_create() brings the core services
// up and assembles the toolkit + window; fl_app_run() blocks, calling our frame callback once per frame until
// the window is closed.
//
// Toolkit scaffolding, run manually - not a CI test. Built with FLOWI_BUILD_EXAMPLES.

#include "app/app.h"
#include <core/core.h>
#include <core/log.h>
#include "ui/color.h"
#include "ui/layout.h"

static void frame(void* user_data) {
    (void)user_data;

    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }, .color = ui_color(24, 24, 32, 255),
                   .child_alignment = ui_align(Center, Center)) {
        ui_layout_area(.sizing = { .width = sizing_fixed(240), .height = sizing_fixed(160) },
                       .color = ui_color(200, 40, 50, 255)) {}
    }
}

int main(void) {
    // renderer is left at its default (FlAppRenderer_Software). On a windowed macOS app fl_app_create promotes
    // that default to FlAppRenderer_Metal; on Linux it stays the software windowed backend.
    FlAppSettings settings = {
        .window_title = FL_S("fl_app smoke"),
        .width = 1280,
        .height = 720,
        .headless = false,
    };

    FlApp* app = fl_app_create(&settings);
    if (!app) {
        log_error("app_smoke: fl_app_create failed");
        return 1;
    }

    log_info("app_smoke: window up at %dx%d — close the window to exit", settings.width, settings.height);
    fl_app_run(app, nullptr, frame);

    fl_app_destroy(app);
    return 0;
}
