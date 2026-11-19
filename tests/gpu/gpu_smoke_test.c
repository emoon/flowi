// Headless smoke test for the GPU render backend.
//
// Section 1: packer and translator unit tests. Pure CPU, runs before any EGL/GL call.
//
// Section 2: the all-compute pipeline end to end on a GL 4.3 core context created via EGL surfaceless. There is
// no window and no native display, so it runs on whatever driver EGL picks, which is llvmpipe on a CI runner
// without a GPU (LIBGL_ALWAYS_SOFTWARE=1 forces it elsewhere).

#include "gpu_test_common.h"

typedef enum SuiteSig {
    SuiteSig_Standalone,
    SuiteSig_Backend,
    SuiteSig_BackendIr,
    SuiteSig_ArenaBackend,
} SuiteSig;

typedef struct Suite {
    SuiteSig sig;
    union {
        int (*standalone)(void);
        int (*backend)(FlRenderBackend*);
        int (*backend_ir)(FlRenderBackend*, FlArena*);
        int (*arena_backend)(FlArena*, FlRenderBackend*);
    } fn;
} Suite;

typedef struct SuiteEnv {
    FlArena* arena;
    FlRenderBackend* backend;
    FlArena* ir_arena;
} SuiteEnv;

#define STANDALONE(f) { .sig = SuiteSig_Standalone, .fn.standalone = (f) }
#define BACKEND(f) { .sig = SuiteSig_Backend, .fn.backend = (f) }
#define BACKEND_IR(f) { .sig = SuiteSig_BackendIr, .fn.backend_ir = (f) }
#define ARENA_BACKEND(f) { .sig = SuiteSig_ArenaBackend, .fn.arena_backend = (f) }

static int run_suite(const Suite* suite, const SuiteEnv* env) {
    switch (suite->sig) {
        case SuiteSig_Standalone:
            return suite->fn.standalone();
        case SuiteSig_Backend:
            return suite->fn.backend(env->backend);
        case SuiteSig_BackendIr:
            return suite->fn.backend_ir(env->backend, env->ir_arena);
        case SuiteSig_ArenaBackend:
            return suite->fn.arena_backend(env->arena, env->backend);
    }
    return 1;
}

static int run_suites(const Suite* suites, size_t count, const SuiteEnv* env) {
    for (size_t i = 0; i < count; i++) {
        if (run_suite(&suites[i], env) != 0) {
            return 1;
        }
    }
    return 0;
}

#if defined(__APPLE__)

static const Suite k_backend_suites[] = {
    BACKEND_IR(run_painter_rects_pixel_test),
    BACKEND_IR(run_clear_screen_test),
    BACKEND_IR(run_rect_pixel_test),
    BACKEND_IR(run_border_pixel_test),
    BACKEND_IR(run_polyline_pixel_tests),
    BACKEND_IR(run_arc_pixel_tests),
    BACKEND_IR(run_circle_diamond_pixel_tests),
    BACKEND_IR(run_bar_pixel_tests),
    BACKEND_IR(run_gradient_pixel_test),
    BACKEND_IR(run_clip_pixel_test),
    BACKEND_IR(run_image_pixel_test),
    BACKEND(run_glyph_raster_tests),
    BACKEND_IR(run_text_pixel_tests),
    BACKEND_IR(run_text_atlas_tests),
    BACKEND_IR(run_svg_raster_tests),
    BACKEND_IR(run_svg_color_raster_tests),
    BACKEND_IR(run_svg_zoom_tests),
    BACKEND_IR(run_svg_compaction_tests),
    BACKEND_IR(run_svg_large_raster_tests),
    BACKEND_IR(run_slot_image_pixel_tests),
    BACKEND_IR(run_iosurface_surface_test),
    BACKEND_IR(run_iosurface_churn_leak_test),
};

static const Suite k_app_suites[] = {
    STANDALONE(run_em_app_gpu_path_test),
};

#else

static const Suite k_cpu_suites[] = {
    STANDALONE(run_packer_tests),
    STANDALONE(run_glyph_atlas_tests),
    STANDALONE(run_glyph_lower_tests),
    STANDALONE(run_translator_tests),
    STANDALONE(run_effect_translator_tests),
    STANDALONE(run_shape_translator_tests),
    STANDALONE(run_polyline_translator_tests),
    STANDALONE(run_border_translator_tests),
    STANDALONE(run_slots_tests),
    STANDALONE(run_slot_translator_tests),
    STANDALONE(run_background_translator_tests),
    STANDALONE(run_text_translator_tests),
    STANDALONE(run_svg_translator_tests),
};

// Lifecycle must run last: its change_resolution(640, 360) would invalidate the 1920x1080 pixel coordinates and
// tile grid the earlier sections assert against.
static const Suite k_backend_suites[] = {
    BACKEND_IR(run_painter_rects_pixel_test),
    BACKEND_IR(run_clear_screen_test),
    BACKEND_IR(run_rect_pixel_test),
    BACKEND_IR(run_border_pixel_test),
    BACKEND_IR(run_gradient_pixel_test),
    BACKEND_IR(run_clip_pixel_test),
    BACKEND_IR(run_image_pixel_test),
    ARENA_BACKEND(run_ui_frame_test),
    ARENA_BACKEND(run_ui_image_widget_test),
    ARENA_BACKEND(run_menu_flip_test),
    BACKEND_IR(run_pixel_tests),
    BACKEND_IR(run_border_pixel_tests),
    BACKEND_IR(run_arc_pixel_tests),
    BACKEND_IR(run_circle_diamond_pixel_tests),
    BACKEND_IR(run_bar_pixel_tests),
    BACKEND_IR(run_image_tests),
    BACKEND_IR(run_slot_image_pixel_tests),
    BACKEND_IR(run_background_pixel_tests),
    BACKEND_IR(run_effect_pixel_tests),
    BACKEND_IR(run_polyline_pixel_tests),
    BACKEND_IR(run_classification_tests),
    BACKEND(run_glyph_raster_tests),
    BACKEND_IR(run_text_pixel_tests),
    BACKEND_IR(run_text_atlas_tests),
    BACKEND_IR(run_svg_raster_tests),
    BACKEND_IR(run_svg_color_raster_tests),
    BACKEND_IR(run_svg_zoom_tests),
    BACKEND_IR(run_svg_compaction_tests),
    BACKEND_IR(run_svg_large_raster_tests),
    ARENA_BACKEND(run_menu_static_kinds_test),
    BACKEND_IR(run_n_surface_tests),
    BACKEND(run_surface_churn_leak_test),
    BACKEND_IR(run_perf_fence_scenes),
    BACKEND_IR(run_viewport3d_tests),
    BACKEND_IR(run_frame_lifecycle_tests),
};

// fl_app makes its own EGL context current and unbinds it on destroy, so these run after the shared context
// above is torn down.
static const Suite k_app_suites[] = {
    STANDALONE(run_em_app_gpu_path_test),
    STANDALONE(run_viewport3d_app_test),
};

#endif

static int run_backend_suites(FlArena* arena, FlRenderBackend* backend) {
    FlArena* ir_arena = arena_new();
    const SuiteEnv env = { .arena = arena, .backend = backend, .ir_arena = ir_arena };
    if (run_suites(k_backend_suites, sizeof_array(k_backend_suites), &env) != 0) {
        return 1;
    }
    fl_arena_destroy(ir_arena);
    backend->destroy(backend->user_data);
    return 0;
}

#if defined(__APPLE__)

static int run_platform_suites(FlArena* arena) {
    FlRenderBackend backend = create_gpu_renderer(arena);
    CHECK(backend.render != nullptr, "create_gpu_renderer (Metal) failed — no MTLDevice or queue");
    return run_backend_suites(arena, &backend);
}

#else

static int run_platform_suites(FlArena* arena) {
    const SuiteEnv cpu_env = { .arena = arena };
    if (run_suites(k_cpu_suites, sizeof_array(k_cpu_suites), &cpu_env) != 0) {
        return 1;
    }

    EGLDisplay dpy = create_surfaceless_gl43_context();
    CHECK(dpy != EGL_NO_DISPLAY, "EGL surfaceless GL 4.3 context creation failed");

    if (run_partial_compile_failure_test() != 0) {
        return 1;
    }

    FlRenderBackend backend = create_gpu_renderer(arena);
    CHECK(backend.render != nullptr, "create_gpu_renderer failed (GL floor or shader compile)");
    if (run_backend_suites(arena, &backend) != 0) {
        return 1;
    }
    eglTerminate(dpy);
    return 0;
}

#endif

int main(void) {
    FlArena* arena = fl_init(1);
    CHECK(arena != nullptr, "fl_init returned null arena");

    if (run_platform_suites(arena) != 0) {
        return 1;
    }

    const SuiteEnv app_env = { .arena = arena };
    if (run_suites(k_app_suites, sizeof_array(k_app_suites), &app_env) != 0) {
        return 1;
    }

    fl_arena_destroy(arena);

    printf("gpu_smoke_test: PASS\n");
    return 0;
}
