#include "app/app_gpu.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GPU Surface Binding for fl_app. Compiled to nothing unless FL_APP_HAVE_GPU (OpenGL_EGL_FOUND). Holds the
// headless EGL-surfaceless context plus two thin host helpers - backend construction against an already-current
// context, and the surface-texture resolve - so app.c stays free of EGL/GL types. The windowed SDL-GL window +
// context + compositor + present live in the SDL WindowBackend (src/platform/backend_sdl_window.c).
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if FL_APP_HAVE_GPU

#include <core/arena.h>
#include <core/log.h>
#include <render/gpu/gpu_renderer.h>

#ifdef __APPLE__

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// macOS: the GPU backend is Metal. Its surface binding is a bare MTLDevice - no window and no GL context -
// owned inside the render backend itself (gpu_metal.m), so the headless bootstrap has nothing of its own to
// hold and just constructs the backend.

// No context to own on Metal. A one-field placeholder so the headless caller's success check (a non-NULL
// handle) works uniformly across both platforms.
struct FlAppGpu {
    int unused;
};

FlAppGpu* fl_app_gpu_create_headless(struct FlArena* arena, FlRenderBackend* out_backend) {
    *out_backend = (FlRenderBackend) { 0 };

    FlAppGpu* gpu = arena_alloc_zero(arena, FlAppGpu);
    if (!fl_app_gpu_create_backend(arena, out_backend)) {
        return NULL;
    }
    return gpu;
}

bool fl_app_gpu_create_backend(struct FlArena* arena, FlRenderBackend* out_backend) {
    *out_backend = create_gpu_renderer(arena);
    if (out_backend->render == NULL) {
        log_error("fl_app gpu: create_gpu_renderer failed — no MTLDevice or command-queue creation error");
        return false;
    }
    return true;
}

bool fl_app_gpu_surface_texture(FlSurface* surface, void** handle, u32* width, u32* height) {
    // Metal's surface texture is an opaque id<MTLTexture> carried as a void*: the render backend owns the
    // retain, the compositor only samples it. The headless path never calls this (its readback IS the surface).
    GpuSurfaceTexture surf = gpu_renderer_surface_texture(surface);
    if (handle) {
        *handle = surf.handle;
    }
    if (width) {
        *width = (u32)surf.width;
    }
    if (height) {
        *height = (u32)surf.height;
    }
    return true;
}

void fl_app_gpu_destroy(FlAppGpu* gpu) {
    // Nothing to tear down: the MTLDevice + command queue are owned and released by the render backend's destroy().
    (void)gpu;
}

#else // !__APPLE__

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FlAppGpu {
    // Headless: the EGL surfaceless display + context. The windowed path's GL context lives in the SDL
    // WindowBackend.
    EGLDisplay egl_dpy;
    EGLContext egl_ctx;
};

// EGL surfaceless GL 4.3 core context - no window, no native display (CI's llvmpipe). Returns false (leaving
// *dpy/*ctx at EGL_NO_*) on any failure.
static bool create_surfaceless_gl43(EGLDisplay* out_dpy, EGLContext* out_ctx) {
    *out_dpy = EGL_NO_DISPLAY;
    *out_ctx = EGL_NO_CONTEXT;

    const char* client_exts = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    if (client_exts == NULL || strstr(client_exts, "EGL_MESA_platform_surfaceless") == NULL) {
        log_error("fl_app gpu: EGL_MESA_platform_surfaceless not available");
        return false;
    }

    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display
        = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (get_platform_display == NULL) {
        log_error("fl_app gpu: eglGetPlatformDisplayEXT not exported by the EGL driver");
        return false;
    }
    EGLDisplay dpy = get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL)) {
        log_error("fl_app gpu: no surfaceless EGL display");
        return false;
    }
    eglBindAPI(EGL_OPENGL_API);

    static const EGLint config_attrs[] = { EGL_SURFACE_TYPE, 0, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE };
    EGLConfig config;
    EGLint num_configs = 0;
    eglChooseConfig(dpy, config_attrs, &config, 1, &num_configs);

    static const EGLint context_attrs[] = {
        EGL_CONTEXT_MAJOR_VERSION,           4,        EGL_CONTEXT_MINOR_VERSION, 3, EGL_CONTEXT_OPENGL_PROFILE_MASK,
        EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE,
    };
    EGLContext context = eglCreateContext(dpy, num_configs > 0 ? config : NULL, EGL_NO_CONTEXT, context_attrs);
    if (context == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, context)) {
        log_error("fl_app gpu: failed to create/current a GL 4.3 core context");
        eglTerminate(dpy);
        return false;
    }

    *out_dpy = dpy;
    *out_ctx = context;
    return true;
}

FlAppGpu* fl_app_gpu_create_headless(struct FlArena* arena, FlRenderBackend* out_backend) {
    *out_backend = (FlRenderBackend) { 0 };

    FlAppGpu* gpu = arena_alloc_zero(arena, FlAppGpu);
    gpu->egl_dpy = EGL_NO_DISPLAY;
    gpu->egl_ctx = EGL_NO_CONTEXT;

    if (!create_surfaceless_gl43(&gpu->egl_dpy, &gpu->egl_ctx)) {
        return NULL;
    }

    // The context is now current.
    if (!fl_app_gpu_create_backend(arena, out_backend)) {
        fl_app_gpu_destroy(gpu);
        return NULL;
    }
    return gpu;
}

bool fl_app_gpu_create_backend(struct FlArena* arena, FlRenderBackend* out_backend) {
    *out_backend = create_gpu_renderer(arena);
    if (out_backend->render == NULL) {
        log_error("fl_app gpu: create_gpu_renderer failed — GL below 4.3 or shader compile error");
        return false;
    }
    return true;
}

bool fl_app_gpu_surface_texture(FlSurface* surface, u32* texture, u32* width, u32* height) {
    GpuSurfaceTexture surf = gpu_renderer_surface_texture(surface);
    if (texture) {
        *texture = surf.handle;
    }
    if (width) {
        *width = (u32)surf.width;
    }
    if (height) {
        *height = (u32)surf.height;
    }
    return true;
}

void fl_app_gpu_destroy(FlAppGpu* gpu) {
    if (gpu == NULL) {
        return;
    }

    if (gpu->egl_dpy != EGL_NO_DISPLAY) {
        eglMakeCurrent(gpu->egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (gpu->egl_ctx != EGL_NO_CONTEXT) {
            eglDestroyContext(gpu->egl_dpy, gpu->egl_ctx);
        }
        eglTerminate(gpu->egl_dpy);
        gpu->egl_dpy = EGL_NO_DISPLAY;
        gpu->egl_ctx = EGL_NO_CONTEXT;
    }
}

#endif // __APPLE__

#else // !FL_APP_HAVE_GPU

// Keep the translation unit non-empty (and the build's source list uniform) when the GPU path is compiled out.
typedef int fl_app_gpu_translation_unit_not_empty;

#endif // FL_APP_HAVE_GPU
