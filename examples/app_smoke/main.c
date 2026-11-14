// Uses the fl_app runner directly from C. fl_app_create() starts the core services and opens an SDL window;
// fl_app_run() blocks and calls the frame callback once per frame until the window is closed.
//
// Run by hand; it is not a CI test. Built with FLOWI_BUILD_EXAMPLES.

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
    // .renderer is left at its default, FlAppRenderer_Software. On macOS fl_app_create promotes a windowed app
    // to FlAppRenderer_Metal; on Linux the app stays on the software windowed backend.
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
