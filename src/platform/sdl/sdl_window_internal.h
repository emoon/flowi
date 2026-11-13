#pragma once

#if BACKEND_USE_SDL

// Types shared by the SDL window backend's common file and its per-mode files (GL, Metal, software).

#include "../window_backend.h"
#include "sdl_input_common.h"
#include <core/arena.h>
#include <core/core.h>
#include <SDL.h>

#if FL_WINDOW_HAVE_GPU
#include "sdl_window_gl_compositor.h"
#endif

#if FL_WINDOW_HAVE_METAL
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#endif

// Per-window handle - the OS window the host holds opaque and passes back to the per-window seams. window_id
// demuxes the shared OS event queue per window.

typedef struct FlWindow {
    SDL_Window* window;
    u32 window_id;
    SDL_Renderer* renderer;  // software mode only (null in GL/Metal mode); owns the streaming blit
    SDL_Texture* ui_texture; // software mode: streaming RGB24, (re)created to match the source size
    int tex_w;               // current ui_texture size, so a live resize triggers a recreate
    int tex_h;
#if FL_WINDOW_HAVE_METAL
    SDL_MetalView metal_view;  // Metal mode: the SDL Metal view attached to the window; owns the CAMetalLayer
    CAMetalLayer* metal_layer; // Metal mode: the layer present pulls drawables from (owned by the view; not retained)
#endif
} FlWindow;

// SDL Window Backend context - the OS connection. In GL mode the single shared GL context + the compositor
// program/VAO also live here, built with the first window and torn down with the last.

typedef struct SdlWindowMode SdlWindowMode;

typedef struct SdlWindowContext {
    struct FlArena* arena;
    SdlInputCommon input;
    const SdlWindowMode* ops; // the construction mode: window create/destroy/measure + present path

#if FL_WINDOW_HAVE_GPU
    SDL_GLContext gl_ctx; // the single shared GL context; null until the first window creates it
    SdlCompositorGl gl;   // compositor GL entry points, resolved once with the shared context
    GLuint program;       // fullscreen-quad compositor program (shared)
    GLuint vao;           // empty VAO the core profile needs to draw (shared)
    bool gl_ready;        // the shared context + compositor are built
    Uint64 last_present;  // perf-counter at the last swap (0 = none) - drives the adaptive frame-time cap
    int window_count;     // live windows; the last one destroyed tears the shared GL state down
#endif
#if FL_WINDOW_HAVE_METAL
    // Shared Metal compositor state, built lazily on the first present against the render backend's device (read
    // from the Surface Texture's .device so the sampled texture and the compositor share one MTLDevice). One
    // queue + one pipeline serve every window's CAMetalLayer. +1 retained; released when the last window is
    // destroyed.
    id<MTLDevice> metal_device;
    id<MTLCommandQueue> metal_queue;
    id<MTLRenderPipelineState> metal_pipeline;
    bool metal_ready;       // device + queue + pipeline built
    int metal_window_count; // live Metal windows; the last one destroyed releases the shared compositor state
#endif
} SdlWindowContext;

// What a window is opened with, resolved from the FlWindowConfig.
typedef struct SdlWindowParams {
    const char* title;
    int pos_x;
    int pos_y;
    int width;
    int height;
    u32 fullscreen_flag; // SDL_WINDOW_FULLSCREEN_DESKTOP or 0
    bool resizable;
} SdlWindowParams;

// The type of FlWindowBackend.present, which takes the Surface Texture as a Metal handle on APPLE and a GL texture
// name elsewhere.
#if __APPLE__
typedef void (*SdlPresentFn)(void* ud, FlWindow* window, void* surface_texture, u32 tex_width, u32 tex_height);
#else
typedef void (*SdlPresentFn)(void* ud, FlWindow* window, u32 surface_texture, u32 tex_width, u32 tex_height);
#endif

// One construction mode: how its windows are created, destroyed and measured, and the present slots it fills in the
// FlWindowBackend (the slots a mode does not use stay null).
struct SdlWindowMode {
    FlWindowBackendMode mode;
    const char* name;
    // Returns false on failure (logged); the window is left without an OS window.
    bool (*create_window)(SdlWindowContext* self, FlWindow* window, const SdlWindowParams* params);
    // Releases the mode's per-window and shared state; the common code then destroys the OS window.
    void (*destroy_window)(SdlWindowContext* self, FlWindow* window);
    // Live drawable size in physical pixels.
    void (*pixel_size)(FlWindow* window, int* px_w, int* px_h);
    SdlPresentFn present;
    void (*present_software)(void* ud, FlWindow* window, const void* px, u32 tex_width, u32 tex_height, u32 stride);
    void (*make_current)(void* ud, FlWindow* window);
};

#if FL_WINDOW_HAVE_GPU
extern const SdlWindowMode k_sdl_window_mode_gl;
#endif
#if FL_WINDOW_HAVE_METAL
extern const SdlWindowMode k_sdl_window_mode_metal;
#endif
#if FL_WINDOW_HAVE_SOFTWARE
extern const SdlWindowMode k_sdl_window_mode_software;
#endif

// Opens the OS window every mode starts from: the params' geometry, the high-DPI flag, resizable when asked, and
// the mode's own SDL_WINDOW_* flags. Fills window->window and window->window_id; mode_label names the mode in
// the failure log.
bool sdl_create_base_window(FlWindow* window, const SdlWindowParams* params, u32 mode_flags, const char* mode_label);

#endif // BACKEND_USE_SDL
