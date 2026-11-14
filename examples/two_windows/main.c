// Two OS windows driven by one fl_app.
//
// The main window (opened by fl_app_create) and a second one opened with fl_app_window_open each have their own
// frame callback and state. fl_app pumps the shared event queue once per tick, routes events to each window by
// id, and ticks and presents both from the one shared GPU backend. Closing the second window leaves the main one
// running; closing the main window quits.
//
// Run by hand; it is not a CI test. unit_tests covers headless multi-window (app.multi_window_*). GPU only: the
// compute backend aborts under a software rasterizer. Run from the repo root so the working-directory asset
// mount finds the bundled font.

#include <stdio.h>
#include <string.h>

#include "app/app.h"
#include <flowi/ui/font.h>
#include <core/core.h>
#include <core/log.h>
#include "ui/color.h"
#include "ui/layout.h"
#include "ui/ui.h"

typedef struct WindowState {
    const char* title;
    FlColor background;
    FlColor accent;
    FlFontId font; // the shared outline font, set active on this window's context each frame
    int clicks;
} WindowState;

// Called when the window manager asks to close window two. Returning true lets the close proceed; returning
// false vetoes it, for example to show an unsaved-changes prompt first.
static bool window_two_on_close(void* user_data) {
    WindowState* st = (WindowState*)user_data;
    log_info("two_windows: window two closing (had %d clicks) — approving", st->clicks);
    return true;
}

static void window_frame(void* user_data) {
    WindowState* st = (WindowState*)user_data;

    // The font lives in the shared font cache, but the active font is per window context, so set it every frame.
    ui_font_set(st->font);

    char buf[64];
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }, .color = st->background,
                   .direction = LayoutDirection_TopToBottom, .child_gap = 16, .padding = padding_all(24),
                   .child_alignment = ui_align(Center, Center)) {
        ui_text_styled(fl_frame_str(st->title, strlen(st->title)), .color = ui_color(235, 235, 245, 255));

        if (ui_button(FL_S("Click me"), .color = st->accent).clicked) {
            st->clicks++;
        }

        int n = snprintf(buf, sizeof(buf), "clicks: %d", st->clicks);
        ui_text(fl_frame_str(buf, (size_t)n));
    }
}

int main(void) {
    // Multi-window present composites each window's surface texture through the SDL window backend, which needs
    // the GPU renderer. There is no software multi-window path.
    FlAppSettings settings = {
        .window_title = FL_S("flowi — window one"),
        .width = 640,
        .height = 480,
        .headless = false,
        .renderer = FlAppRenderer_Gpu,
    };
    FlApp* app = fl_app_create(&settings);
    if (!app) {
        log_error("two_windows: fl_app_create failed (GPU renderer required; build with the GPU backend)");
        return 1;
    }

    // The GPU backend computes glyph coverage from outlines, so the built-in 1-bit default font does not render.
    // Load a TTF through the runner's working-directory asset mount. The load is async; wait for it so the first
    // frames lay out with the font ready.
    FlFontId font = fl_font_load(FL_S("examples/data/Roboto-Regular.ttf"), 20);
    ui_font_set(font);
    ui_font_wait_load(font);

    WindowState main_state = {
        .title = "Window one (close to quit)",
        .background = ui_color(24, 26, 34, 255),
        .accent = ui_color(70, 130, 200, 255),
        .font = font,
    };
    WindowState second_state = {
        .title = "Window two (close to dismiss)",
        .background = ui_color(34, 26, 24, 255),
        .accent = ui_color(200, 110, 70, 255),
        .font = font,
    };

    // Offset the second window's position so it does not stack on the main window.
    FlWindowSettings second = {
        .title = FL_S("flowi — window two"),
        .width = 520,
        .height = 400,
        .x = 760,
        .y = 160,
    };
    FlAppWindow* second_window = fl_app_window_open(app, &second, window_frame, &second_state);
    if (!second_window) {
        log_error("two_windows: fl_app_window_open failed");
        fl_app_destroy(app);
        return 1;
    }
    fl_app_window_set_on_close(app, second_window, window_two_on_close, &second_state);

    log_info("two_windows: two GPU windows up — click the button in each (independent counters); "
             "close window two to dismiss it, window one to quit");
    fl_app_run(app, &main_state, window_frame);

    fl_app_destroy(app);
    return 0;
}
