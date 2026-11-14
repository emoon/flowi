// SDL window backend, GL mode: SDL-GL windows sharing one GL context and the fullscreen-quad compositor. Present
// composites the GPU render backend's Surface Texture (a plain u32) over the window and swaps.

#include "sdl_window_internal.h"

#if BACKEND_USE_SDL && FL_WINDOW_HAVE_GPU

#include <core/log.h>

// The shared context and compositor are built with the first window.
static bool sdl_gl_create_shared_state(SdlWindowContext* self, SDL_Window* window) {
    self->gl_ctx = SDL_GL_CreateContext(window);
    if (self->gl_ctx == NULL) {
        log_error("fl_app window: SDL_GL_CreateContext failed: %s", SDL_GetError());
        return false;
    }
    // Best-effort vsync; sdl_present_gl applies the display-refresh frame cap regardless.
    if (SDL_GL_SetSwapInterval(1) != 0) {
        log_info("fl_app window: vsync rejected (%s); pacing via the display-refresh frame cap", SDL_GetError());
    }
    if (!sdl_compositor_gl_load(&self->gl)) {
        SDL_GL_DeleteContext(self->gl_ctx);
        self->gl_ctx = NULL;
        return false;
    }
    self->program = sdl_build_compositor_program(&self->gl);
    if (self->program == 0) {
        SDL_GL_DeleteContext(self->gl_ctx);
        self->gl_ctx = NULL;
        return false;
    }
    self->gl.GenVertexArrays(1, &self->vao); // core profile needs a bound VAO to draw, even with no attributes
    self->gl_ready = true;
    return true;
}

// EGL-backed GL 4.3 core context. Only the first window creates it; later windows make it current against their
// own drawable.
static bool sdl_gl_create_window(SdlWindowContext* self, FlWindow* window, const SdlWindowParams* params) {
    SDL_SetHint(SDL_HINT_VIDEO_X11_FORCE_EGL, "1");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    if (!sdl_create_base_window(window, params, SDL_WINDOW_OPENGL, "gl")) {
        return false;
    }
    if (!self->gl_ready && !sdl_gl_create_shared_state(self, window->window)) {
        SDL_DestroyWindow(window->window);
        return false;
    }
    self->window_count++;
    return true;
}

// The last window deletes the shared GL state with the context current against this window's drawable.
static void sdl_gl_destroy_window(SdlWindowContext* self, FlWindow* window) {
    self->window_count--;
    if (self->window_count > 0 || !self->gl_ready) {
        return;
    }
    SDL_GL_MakeCurrent(window->window, self->gl_ctx);
    if (self->vao != 0) {
        self->gl.DeleteVertexArrays(1, &self->vao);
        self->vao = 0;
    }
    if (self->program != 0) {
        self->gl.DeleteProgram(self->program);
        self->program = 0;
    }
    SDL_GL_DeleteContext(self->gl_ctx);
    self->gl_ctx = NULL;
    self->gl_ready = false;
}

static bool sdl_gl_window_usable(const SdlWindowContext* self, const FlWindow* window) {
    return window != NULL && window->window != NULL && self->gl_ready;
}

// Bind the shared GL context against a window's drawable without compositing or swapping.
static void sdl_make_current(void* ud, FlWindow* window) {
    SdlWindowContext* self = (SdlWindowContext*)ud;
    if (!sdl_gl_window_usable(self, window)) {
        return;
    }
    SDL_GL_MakeCurrent(window->window, self->gl_ctx);
}

// Composite the UI surface texture over the window with premultiplied src-over blending, setting all GL state per
// call.
static void sdl_gl_composite(SdlWindowContext* self, SDL_Window* window, GLuint surface_texture) {
    int draw_w = 0;
    int draw_h = 0;
    SDL_GL_GetDrawableSize(window, &draw_w, &draw_h);
    glViewport(0, 0, draw_w, draw_h);
    glClearColor(0.06f, 0.06f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    self->gl.ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, surface_texture);
    // Nearest filtering; the surface has a single mip level.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    self->gl.UseProgram(self->program);
    self->gl.Uniform1i(self->gl.GetUniformLocation(self->program, "ui_tex"), 0);
    self->gl.BindVertexArray(self->vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // Unbind the compositor's state.
    glDisable(GL_BLEND);
    self->gl.BindVertexArray(0);
    self->gl.UseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// The refresh rate of the display the window is on, or 0 when unknown.
static int sdl_window_refresh_rate(SDL_Window* window) {
    SDL_DisplayMode mode;
    const int display_index = SDL_GetWindowDisplayIndex(window);
    if (display_index >= 0 && SDL_GetCurrentDisplayMode(display_index, &mode) == 0) {
        return mode.refresh_rate;
    }
    return 0;
}

// Frame cap: sleep out the remainder of the display's refresh period since the last swap (shared across windows).
static void sdl_gl_cap_frame_rate(SdlWindowContext* self, SDL_Window* window) {
    const int refresh = sdl_window_refresh_rate(window);
    const double target = 1.0 / (refresh > 0 ? (double)refresh : 60.0); // 60 Hz when the refresh rate is unknown
    const Uint64 now = SDL_GetPerformanceCounter();
    if (self->last_present != 0) {
        const double elapsed = (double)(now - self->last_present) / (double)SDL_GetPerformanceFrequency();
        if (elapsed < target) {
            const Uint32 sleep_ms = (Uint32)((target - elapsed) * 1000.0);
            if (sleep_ms > 0) {
                SDL_Delay(sleep_ms);
            }
        }
    }
    self->last_present = SDL_GetPerformanceCounter();
}

static void sdl_present_gl(void* ud, FlWindow* window, FlSurfaceHandle surface_texture) {
    SdlWindowContext* self = (SdlWindowContext*)ud;
    // Texture name 0 is a surface that has not rendered yet; leave the window untouched.
    if (!sdl_gl_window_usable(self, window) || surface_texture == 0) {
        return;
    }
    // Bind the shared context against this window's drawable, then composite + swap.
    SDL_GL_MakeCurrent(window->window, self->gl_ctx);
    sdl_gl_composite(self, window->window, (GLuint)surface_texture);
    SDL_GL_SwapWindow(window->window);
    sdl_gl_cap_frame_rate(self, window->window);
}

static void sdl_gl_pixel_size(FlWindow* window, int* px_w, int* px_h) {
    SDL_GL_GetDrawableSize(window->window, px_w, px_h);
}

const SdlWindowMode k_sdl_window_mode_gl = {
    .mode = FlWindowBackendMode_Gl,
    .name = "GL",
    .create_window = sdl_gl_create_window,
    .destroy_window = sdl_gl_destroy_window,
    .pixel_size = sdl_gl_pixel_size,
    .present = sdl_present_gl,
    .make_current = sdl_make_current,
};

#endif // BACKEND_USE_SDL && FL_WINDOW_HAVE_GPU
