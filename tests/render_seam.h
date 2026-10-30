// Shared funnels over the FlRenderBackend renderer seam (render / read_pixels / change_resolution).
#pragma once

#include <ui/render_backend.h>

static inline void render_seam_render(FlRenderBackend* backend, FlSurface* surface, const FlRenderFrame* frame) {
    backend->render(backend->user_data, surface, frame);
}

static inline bool render_seam_read_pixels(FlRenderBackend* backend, FlSurface* surface, FlPixelReadback* out) {
    return backend->read_pixels(backend->user_data, surface, out);
}

static inline void render_seam_change_resolution(FlRenderBackend* backend, FlSurface* surface, int width, int height) {
    backend->change_resolution(backend->user_data, surface, width, height);
}
