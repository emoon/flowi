// GPU smoke test: EGL surfaceless host plumbing and the partial shader-compile failure cleanup test.

#include "gpu_test_common.h"

#if !defined(__APPLE__)

// Section 2 host plumbing: a GL 4.3 core context on the EGL surfaceless platform (no window, no native
// display, which is what CI's llvmpipe runs).

static EGLDisplay open_surfaceless_display(void) {
    const char* client_exts = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    if (client_exts == nullptr || strstr(client_exts, "EGL_MESA_platform_surfaceless") == nullptr) {
        fprintf(stderr, "gpu_smoke_test: EGL_MESA_platform_surfaceless not available\n");
        return EGL_NO_DISPLAY;
    }

    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display
        = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (get_platform_display == nullptr) {
        fprintf(stderr, "gpu_smoke_test: eglGetPlatformDisplayEXT not exported by the EGL driver\n");
        return EGL_NO_DISPLAY;
    }
    EGLDisplay dpy = get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, nullptr, nullptr)) {
        fprintf(stderr, "gpu_smoke_test: no surfaceless EGL display\n");
        return EGL_NO_DISPLAY;
    }
    return dpy;
}

EGLDisplay create_surfaceless_gl43_context(void) {
    EGLDisplay dpy = open_surfaceless_display();
    if (dpy == EGL_NO_DISPLAY) {
        return EGL_NO_DISPLAY;
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
    EGLContext context = eglCreateContext(dpy, num_configs > 0 ? config : nullptr, EGL_NO_CONTEXT, context_attrs);
    if (context == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, context)) {
        fprintf(stderr, "gpu_smoke_test: failed to create/current a GL 4.3 core context\n");
        eglTerminate(dpy);
        return EGL_NO_DISPLAY;
    }
    return dpy;
}

// Partial shader-compile failure: when one of the four programs fails to compile, create_gpu_renderer must
// delete the programs that did compile before returning the zeroed backend. The GpuShared is the failed arena's
// first allocation, so its address is recoverable from the arena cursor; the stale program IDs it still holds
// let glIsProgram prove the survivors were deleted (not just abandoned).

int run_partial_compile_failure_test(void) {
    FlArena* fail_arena = arena_new();
    GpuShared* shared = (GpuShared*)fl_arena_aligned_pos(fail_arena, _Alignof(GpuShared));

    gpu_shaders_test_force_color_svg_failure(true);
    FlRenderBackend failed = create_gpu_renderer(fail_arena);
    gpu_shaders_test_force_color_svg_failure(false);

    CHECK(failed.render == nullptr, "partial compile failure: create_gpu_renderer returns the zeroed backend");
    CHECK(shared->classify_prog != 0 && shared->raster_prog != 0 && shared->glyph_prog != 0,
          "partial compile failure: the other three programs compiled (the failure really was partial)");
    CHECK(shared->color_svg_prog == 0, "partial compile failure: the color svg program failed to compile");

    PFNGLISPROGRAMPROC is_program = (PFNGLISPROGRAMPROC)eglGetProcAddress("glIsProgram");
    CHECK(is_program != nullptr, "partial compile failure: glIsProgram resolves");
    CHECK(!is_program(shared->classify_prog) && !is_program(shared->raster_prog) && !is_program(shared->glyph_prog),
          "partial compile failure: the compiled programs were deleted (no GL program leak)");

    fl_arena_destroy(fail_arena);
    printf("gpu_smoke_test: partial shader-compile failure cleanup PASS\n");
    return 0;
}

#endif // !defined(__APPLE__)
