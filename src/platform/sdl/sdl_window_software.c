// SDL window backend, software mode: plain SDL_Renderer windows (no GL context). Present blits the software render
// backend's offscreen RGB24 pixel buffer through a streaming texture.

#include "sdl_window_internal.h"

#if BACKEND_USE_SDL && FL_WINDOW_HAVE_SOFTWARE

#include <core/log.h>

static bool sdl_software_create_window(SdlWindowContext* self, FlWindow* window, const SdlWindowParams* params) {
    UNUSED(self);
    if (!sdl_create_base_window(window, params, 0, "software")) {
        return false;
    }
    // The streaming texture is created on the first present_software.
    window->renderer = SDL_CreateRenderer(window->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (window->renderer == NULL) {
        log_error("fl_app window: SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(window->window);
        return false;
    }
    return true;
}

static void sdl_software_destroy_window(SdlWindowContext* self, FlWindow* window) {
    UNUSED(self);
    if (window->renderer == NULL) {
        return;
    }
    if (window->ui_texture != NULL) {
        SDL_DestroyTexture(window->ui_texture);
        window->ui_texture = NULL;
    }
    SDL_DestroyRenderer(window->renderer);
    window->renderer = NULL;
}

static void sdl_software_pixel_size(FlWindow* window, int* px_w, int* px_h) {
    SDL_GetRendererOutputSize(window->renderer, px_w, px_h);
}

static bool sdl_software_texture_matches(const FlWindow* window, u32 tex_width, u32 tex_height) {
    if (window->ui_texture == NULL) {
        return false;
    }
    return (u32)window->tex_w == tex_width && (u32)window->tex_h == tex_height;
}

// Makes ui_texture match the source size, recreating it on a size change. False if the texture could not be created.
static bool sdl_software_ensure_texture(FlWindow* window, u32 tex_width, u32 tex_height) {
    if (sdl_software_texture_matches(window, tex_width, tex_height)) {
        return true;
    }
    if (window->ui_texture != NULL) {
        SDL_DestroyTexture(window->ui_texture);
        window->ui_texture = NULL;
    }
    window->ui_texture = SDL_CreateTexture(window->renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING,
                                           (int)tex_width, (int)tex_height);
    if (window->ui_texture == NULL) {
        log_error("fl_app window: streaming texture creation failed (%ux%u): %s", tex_width, tex_height,
                  SDL_GetError());
        window->tex_w = 0;
        window->tex_h = 0;
        return false;
    }
    SDL_SetTextureScaleMode(window->ui_texture, SDL_ScaleModeNearest); // crisp 1:1 UI->window blit
    window->tex_w = (int)tex_width;
    window->tex_h = (int)tex_height;
    return true;
}

static void sdl_present_software(void* ud, FlWindow* window, const void* px, u32 tex_width, u32 tex_height,
                                 u32 stride) {
    UNUSED(ud);
    if (window == NULL || window->renderer == NULL) {
        return;
    }
    if (px == NULL) {
        return;
    }
    if (tex_width == 0 || tex_height == 0) {
        return;
    }
    if (!sdl_software_ensure_texture(window, tex_width, tex_height)) {
        return;
    }
    SDL_UpdateTexture(window->ui_texture, NULL, px, (int)stride);
    SDL_SetRenderDrawColor(window->renderer, 0, 0, 0, 255);
    SDL_RenderClear(window->renderer);
    SDL_RenderCopy(window->renderer, window->ui_texture, NULL, NULL);
    SDL_RenderPresent(window->renderer);
}

const SdlWindowMode k_sdl_window_mode_software = {
    .mode = FlWindowBackendMode_Software,
    .name = "software",
    .create_window = sdl_software_create_window,
    .destroy_window = sdl_software_destroy_window,
    .pixel_size = sdl_software_pixel_size,
    .present_software = sdl_present_software,
};

#endif // BACKEND_USE_SDL && FL_WINDOW_HAVE_SOFTWARE
