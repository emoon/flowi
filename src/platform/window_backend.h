#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// flowi window backend interface - the single OS-integration seam: window lifecycle, the event pump (demuxed per
// window by OS window id), keyboard/mouse/cursor/relative-mouse/warp, per-window drawable size + content scale,
// and presentation. One backend owns the OS connection and creates per-window handles.
//
// Each host-level window pairs one window-backend window + one render-backend FlSurface + one per-window FlUiCtx.

#include <stdbool.h>

#include <core/string.h>
#include <core/types.h>
#include "input_types.h"

struct FlArena;
struct FlUiCtx;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Opaque, backend-owned OS-window handle. Each concrete backend defines its own struct for this incomplete type
// and casts internally - the host never sees its layout.

typedef struct FlWindow FlWindow;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Window creation parameters - title plus the initial drawable size in physical pixels and top-left position.

#define FL_WINDOW_POS_DEFAULT (-2147483647 - 1) // INT32_MIN: "let the backend choose" sentinel for x/y

typedef struct FlWindowConfig {
    FlString title;
    int width;
    int height;
    int x;           // initial top-left x, or FL_WINDOW_POS_DEFAULT
    int y;           // initial top-left y, or FL_WINDOW_POS_DEFAULT
    bool fullscreen; // open fullscreen at the desktop resolution; width x height is the restored size
    bool resizable;  // the user can resize the window
} FlWindowConfig;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction mode: a backend instance is built for exactly one present path, and the runner selects it by
// whichever present slot is non-null.

typedef enum FlWindowBackendMode {
    FlWindowBackendMode_Gl = 0,   // GPU windows: composite a Surface Texture over a GL window (present)
    FlWindowBackendMode_Software, // software windows: blit a CPU pixel buffer to a plain renderer (present_software)
    FlWindowBackendMode_Metal,    // GPU windows on APPLE: composite a Surface Texture over a CAMetalLayer (present)
} FlWindowBackendMode;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Backend interface - one injected object, per-window seams.

typedef struct FlWindowBackend {
    void* user_data;

    // Window lifecycle. create_window returns null on failure; the host destroys every window before the backend
    // (destroy, below).
    FlWindow* (*create_window)(void* ud, const FlWindowConfig* config);
    void (*destroy_window)(void* ud, FlWindow* window);

    // Event pump - drain the OS event queue ONCE, demux every window-addressed event by OS window id, and
    // translate it into fl_platform_input_push_* on the matching window's FlUiCtx. windows[i] pairs with contexts[i];
    // count is the live window count. windows[0]/contexts[0] is the main window: events that are not
    // window-addressed (SDL_QUIT, gamepad device/axis) route there. A per-window close pushes FlWindowEvent_Close
    // onto only that window's context. Quit/close travels via the per-context quit_requested, which the host polls;
    // there is no backend should_close.
    void (*pump_events)(void* ud, FlWindow** windows, struct FlUiCtx** contexts, int count);

    // Per-window drawable size in physical pixels and content scale (the DPI factor: physical px / logical px).
    void (*drawable_size)(void* ud, FlWindow* window, int* width, int* height);
    float (*content_scale)(void* ud, FlWindow* window);

    // Content scale of the primary display before any window exists, for backends whose window size is in
    // physical pixels. Null (or <= 0) means 1.0: the window size is already in logical units.
    float (*display_content_scale)(void* ud);

    // Cursor + hidden-drag seam, per window. The core resolves a per-frame last-writer-wins request and the host
    // flushes the result here each frame. set_relative_mouse_mode enters (true) / exits (false) raw unbounded
    // motion (the backend then delivers FlMouseEvent_Relative_Motion and hides the visible cursor); warp_mouse
    // moves the OS cursor to (x, y) in window coordinates.
    void (*set_cursor)(void* ud, FlWindow* window, FlCursor cursor);
    void (*set_cursor_visible)(void* ud, FlWindow* window, bool visible);
    void (*set_relative_mouse_mode)(void* ud, FlWindow* window, bool enabled);
    void (*warp_mouse)(void* ud, FlWindow* window, float x, float y);

    // Presentation. A backend provides exactly ONE of the two present slots and the runner selects by whichever is
    // non-null:
    //   present - GPU windows: composite a Surface Texture over the window and swap. The handle is a
    //                      u32 GL texture name on GL, an opaque void* id<MTLTexture> on Metal. The headless backend
    //                      no-ops this.
    //   present_software - software windows: blit a CPU pixel buffer (the software renderer's offscreen RGB24
    //                      target) to the window. px points at tex_width x tex_height pixels with stride bytes
    //                      per row.
#if __APPLE__
    void (*present)(void* ud, FlWindow* window, void* surface_texture, u32 tex_width, u32 tex_height);
#else
    void (*present)(void* ud, FlWindow* window, u32 surface_texture, u32 tex_width, u32 tex_height);
#endif
    void (*present_software)(void* ud, FlWindow* window, const void* px, u32 tex_width, u32 tex_height, u32 stride);

    // Make the backend's shared GL context current against a window's drawable, without compositing or swapping.
    // The host calls this once before render-backend teardown. Null when the backend owns no GL context.
    void (*make_current)(void* ud, FlWindow* window);

    // Backend teardown - called after the host has destroyed every window.
    void (*destroy)(void* ud);
} FlWindowBackend;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory - headless backend. Records cursor/relative-mode/warp state and queues injected events the next pump
// drains.

FlWindowBackend headless_window_backend_create(struct FlArena* arena);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory - SDL backend. Owns the OS connection (SDL video) and creates per-window OS windows; the construction
// mode picks the presentation path:
//   FlWindowBackendMode_Gl - SDL-GL windows carrying a shared GL context + the fullscreen-quad compositor;
//                                  present composites the GPU surface texture and swaps. Requires the GPU build.
//   FlWindowBackendMode_Metal - SDL-Metal windows carrying a CAMetalLayer + the fullscreen-quad MSL compositor;
//                                  present composites onto the layer's drawable. Requires FL_WINDOW_HAVE_METAL.
//   FlWindowBackendMode_Software - plain SDL_Renderer windows (no GL); present_software blits the CPU pixel buffer.
//                                  Requires FL_WINDOW_HAVE_SOFTWARE; gated out on APPLE.
// A mode this build lacks, or an unavailable SDL, returns a zeroed backend (the host's windowed path checks
// create_window before driving it).

FlWindowBackend sdl_window_backend_create(struct FlArena* arena, FlWindowBackendMode mode);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Headless backend test harness - inject events into a specific window's queue (the next pump_events drains it
// into that window's FlUiCtx), and read back the cursor / hidden-drag state the core flushed on the last tick.

void window_backend_headless_inject_mouse_move(FlWindowBackend* backend, FlWindow* window, i32 x, i32 y);
void window_backend_headless_inject_mouse_button(FlWindowBackend* backend, FlWindow* window, FlMouseButton button,
                                                 bool down);
void window_backend_headless_inject_mouse_delta(FlWindowBackend* backend, FlWindow* window, f32 delta_x, f32 delta_y);
void window_backend_headless_inject_wheel(FlWindowBackend* backend, FlWindow* window, f32 x, f32 y);
void window_backend_headless_inject_key(FlWindowBackend* backend, FlWindow* window, FlKey key, bool down);
void window_backend_headless_inject_text(FlWindowBackend* backend, FlWindow* window, FlString text);
void window_backend_headless_inject_drop_file(FlWindowBackend* backend, FlWindow* window, FlString path, f32 x, f32 y);
void window_backend_headless_inject_window_close(FlWindowBackend* backend, FlWindow* window);
void window_backend_headless_inject_window_focus(FlWindowBackend* backend, FlWindow* window, bool gained);

// Set a window's content scale (physical px / logical px); the next fl_app tick recomposes that window's absolute
// UI scale (content_scale x ui_zoom) against it.
void window_backend_headless_set_content_scale(FlWindow* window, float scale);

// Read back the text of the index-th queued (not yet pumped) event in a window's queue - empty string when the
// index is out of range or the event is not a text event.
FlString window_backend_headless_queued_text(FlWindow* window, u32 index);

// Read back the last cursor state and hidden-drag mechanics the core flushed (set_cursor / set_cursor_visible /
// set_relative_mouse_mode / warp_mouse).
FlCursor window_backend_headless_last_cursor(FlWindowBackend* backend);
bool window_backend_headless_cursor_visible(FlWindowBackend* backend);
bool window_backend_headless_relative_mouse_mode(FlWindowBackend* backend);
FlVec2 window_backend_headless_last_warp(FlWindowBackend* backend);
