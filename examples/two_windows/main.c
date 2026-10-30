// examples/two_windows - two real OS windows driven by one fl_app.
//
// The main window (the one fl_app_create opens) shows one UI; a second window opened with fl_app_window_open shows
// a different UI, each with its own per-frame callback + state. fl_app pumps the shared event queue once per tick,
// demuxes events to each window by id, and ticks + presents both from the single shared GPU backend - so both
// windows are independently interactive. Closing the SECOND window leaves the main one running; closing the MAIN
// window quits.
//
// Manual demo only: the monorepo never compiles it and it is not a CI test - the headless multi-window contract is
// covered in unit_tests (app.multi_window_*). GPU-only: the all-compute backend aborts under a software
// rasterizer. Run from the repo root so the working-directory asset mount resolves the bundled font.

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

// Per-window close hook: the host intercepts a window-manager close. Returning true lets the close proceed;
// returning false vetoes it (e.g. to raise an unsaved-changes prompt first).
static bool window_two_on_close(void* user_data) {
    WindowState* st = (WindowState*)user_data;
    log_info("two_windows: window two closing (had %d clicks) — approving", st->clicks);
    return true;
}

static void window_frame(void* user_data) {
    WindowState* st = (WindowState*)user_data;

    // The outline font lives in the shared font cache but must be the active font on THIS window's context; set
    // it every frame so both windows render text regardless of which one was created last.
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
    // GPU renderer: multi-window present composites each window's surface texture through the SDL window backend.
    // There is no software multi-window path - the second window needs the window backend.
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

    // The GPU backend computes glyph coverage from outlines, so the built-in 1-bit default font will not
    // render - load a real TTF through the runner's working-directory asset mount. The load is async; block once so
    // the first frames lay out against a ready font.
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
