#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// fl_app - first-class C runner for the flowi toolkit.
//
// fl_app owns the window, the per-frame update loop, and presentation. C owns the loop and the host is a
// per-frame callback: the host calls fl_app_tick (one frame) or fl_app_run (block until quit) and emits its UI
// inside the supplied frame callback.
//
// Backend selection is driven by FlAppSettings.headless: a windowed app uses the SDL backends (compiled only
// when the bindings build enables FL_APP_HAVE_SDL); a headless app uses the offscreen backends and the
// fl_app_inject_* / fl_app_read_pixels harness surface.

#include <stdbool.h>
#include <stddef.h>

#include <platform/input_types.h>
#include <platform/window_backend.h>
#include <core/string.h>
#include <core/types.h>
#include <flowi/ui/application_settings.h> // @generated FlAppSettings (api_gen)
#include <ui/render_backend.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct FlApp FlApp;

// Per-frame host callback. Emit the UI here (between the core's update_begin/update_end); fl_frame_str is
// valid for the duration of this call.
typedef void (*FlAppFrameFn)(void* user_data);

FlApp* fl_app_create(const FlAppSettings* settings);

// The OS config fl_app_create hands the window backend for the main window: settings width/height are logical
// pixels, scaled by the backend's pre-window display content scale (1.0 when the backend reports none), rounded to
// the nearest pixel.
FlWindowConfig fl_app_main_window_config(const FlAppSettings* settings, const FlWindowBackend* backend);
void fl_app_destroy(FlApp* app);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Multi-window. The app owns a window list: the main window (from fl_app_create) plus any opened with
// fl_app_window_open. Each window pairs one window-backend window + one render surface + one per-window UI
// context, and every fl_app_tick pumps the shared event queue once, demuxes events by window id into each
// window's input, then ticks + presents every window from the shared backend.

typedef struct FlAppWindow FlAppWindow;

// "Let the backend place the window" sentinel for FlWindowSettings.x / .y.
#define FL_APP_WINDOW_POS_DEFAULT (-2147483647 - 1) // INT32_MIN

// Initial settings for a host-opened window: title, drawable size in physical pixels, and an initial top-left
// position (a coordinate of FL_APP_WINDOW_POS_DEFAULT lets the backend place it).
typedef struct FlWindowSettings {
    FlString title;
    int width;
    int height;
    int x; // initial top-left x, or FL_APP_WINDOW_POS_DEFAULT
    int y; // initial top-left y, or FL_APP_WINDOW_POS_DEFAULT
} FlWindowSettings;

// Open another OS window driven by the same shared backend, with its own UI context, render surface, and per-frame
// callback. The app retains it in its window list and ticks + presents it every fl_app_tick. Returns null on
// failure. Closing a non-main window (fl_app_window_close, an injected close, or the window manager) leaves the
// others running; only the main window closing quits the app.
//
// Safe to call mid-frame (from inside a frame callback): a call made while a frame is emitting is QUEUED and the
// OS window + surface + context are created at the next fl_app_tick boundary, never mid-emit. The returned handle
// is valid immediately and goes live (its frame callback starts running) on the next tick. The settings (including
// the title bytes) are copied, so they need not outlive the call.
FlAppWindow* fl_app_window_open(FlApp* app, const FlWindowSettings* settings, FlAppFrameFn frame, void* user_data);

// Request that a host-opened window close. The window is removed from the list and torn down at the start of the
// next fl_app_tick (the same path a window-manager close takes), so it is safe to call from inside a frame
// callback. A window still queued by a mid-frame fl_app_window_open (not yet materialized) is simply dropped from
// the open queue. Closing the main window this way is a no-op - quit by letting the main window's close reach
// fl_app_tick. The window's on_close callback (if any) still runs and may veto the close.
void fl_app_window_close(FlApp* app, FlAppWindow* window);

// Per-window close hook (optional). Invoked at the tick boundary when a window's close is seen - a window-manager
// close, an injected close, or fl_app_window_close - BEFORE the window is torn down (or, for the main window,
// before the app quits). Return true to proceed with the close; return false to VETO it (the window stays and the
// close request is cleared). With no callback the default policy holds: closing the main window quits the app,
// closing any other window closes just it.
typedef bool (*FlAppCloseFn)(void* user_data);

// Install (or clear, with on_close == null) a window's close hook. Works for any window, the main window included
// (fl_app_main_window). user_data is passed to the callback; it is independent of the per-frame callback's user
// data. The hook is consulted on every close path for that window.
void fl_app_window_set_on_close(FlApp* app, FlAppWindow* window, FlAppCloseFn on_close, void* user_data);

// One frame across every window, on one shared delta-time. The main window uses the frame callback passed here;
// every other window uses the one it was opened with. Returns false once the MAIN window's close has been seen and
// not vetoed (the app quit).
bool fl_app_tick(FlApp* app, void* user_data, FlAppFrameFn frame);

// Blocking convenience: while (fl_app_tick(...)) {}
void fl_app_run(FlApp* app, void* user_data, FlAppFrameFn frame);

// User UI-zoom preference: a relative multiplier on top of monitor DPI. 1.0 (the default) is DPI-correct; 1.25
// makes the whole UI 25% bigger regardless of monitor. The absolute scale pushed to the core is
// monitor content scale x zoom, recomposed for every window immediately.
void fl_app_set_ui_zoom(FlApp* app, float zoom);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Headless harness surface - the single injection surface for C + Rust golden tests. The fl_app_inject_*
// entry points assert app->headless (they drive the offscreen input backend).

void fl_app_inject_mouse_move(FlApp* app, i32 x, i32 y);
void fl_app_inject_mouse_button(FlApp* app, FlMouseButton button, bool down);
void fl_app_inject_mouse_delta(FlApp* app, f32 delta_x, f32 delta_y);
void fl_app_inject_wheel(FlApp* app, f32 x, f32 y);
void fl_app_inject_key(FlApp* app, FlKey key, bool down);
void fl_app_inject_text(FlApp* app, FlString text);
void fl_app_inject_window_close(FlApp* app);
void fl_app_inject_drop_file(FlApp* app, FlString path, f32 x, f32 y);

// Headless harness: inject a window-close into a specific app window (the main window, or one returned by
// fl_app_window_open). A close on a non-main window closes only it; a close on the main window quits. Asserts
// headless.
void fl_app_inject_window_close_for(FlApp* app, FlAppWindow* window);

// Headless harness: inject a window focus-gained / focus-lost activation event into a specific app window. A
// deactivated window suppresses its focus ring; activating it restores the field it last had, independent of every
// other window. Asserts headless.
void fl_app_inject_window_focus_for(FlApp* app, FlAppWindow* window, bool gained);

// Headless harness: set a specific app window's content scale (physical px / logical px). The next fl_app_tick
// recomposes that window's absolute UI scale (content_scale x ui_zoom) against it. Asserts headless.
void fl_app_set_window_content_scale_for(FlApp* app, FlAppWindow* window, float scale);

// The app's main window handle (windows[0]). Never null for a live app.
FlAppWindow* fl_app_main_window(FlApp* app);

// Live window enumeration (windows[0] is the main window), reaching windows the app opened internally.
int fl_app_window_count(FlApp* app);
FlAppWindow* fl_app_window_at(FlApp* app, int index);

// Read back the FlCursorShape the core flushed to the input backend on the last
// tick. Asserts app->headless.
FlCursorShape fl_app_headless_cursor_shape(FlApp* app);

// Read back the hidden-drag mechanics the core flushed to the input backend on the last tick.
// relative_mouse_mode is the sticky relative-mode toggle; last_warp is the most recent warp-back target (set on a
// drag end). Both assert app->headless.
bool fl_app_headless_relative_mouse_mode(FlApp* app);
FlVec2 fl_app_headless_last_warp(FlApp* app);

// Pin the per-frame delta-time to a fixed step (headless only): every subsequent fl_app_tick advances time by
// exactly dt seconds instead of sampling the wall clock. Pass a value <= 0 to return to wall-clock timing.
void fl_app_set_test_dt(FlApp* app, float dt);

// Read back the current frame's rendered pixels from the software backend (native interleaved RGB24). Works
// in either mode.
bool fl_app_read_pixels(FlApp* app, FlPixelReadback* out);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copy len bytes into the current frame's arena; valid until the end of the frame. Resolves the arena via the
// process-global UI context (ui_get_ctx()); calling it with no active frame (between fl_app_tick's
// update_begin/update_end) terminates the process in every build type, release included.
FlString fl_frame_str(const char* ptr, size_t len);

// Reserve count uninitialized FlString slots in the current frame's arena; valid until the end of the frame,
// same contract and same assertions as fl_frame_str. The array counterpart for the widgets that take a counted
// FlString array (dropdown options, tab labels, radio options). Returns null for count == 0.
FlString* fl_frame_str_array(uint32_t count);
