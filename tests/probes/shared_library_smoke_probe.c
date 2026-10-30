#include <flowi/app/app.h>
#include <flowi/ui/value_helpers.h>

#include "external_abi_probe.h"

#include <stdint.h>
#include <string.h>

// This probe simulates an external consumer loading libflowi at runtime with
// only public headers available, so it uses raw _WIN32 and the native dynamic
// loader directly rather than core.h/os.h (which have no dynamic-library API).
#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

typedef struct SharedLibraryApi {
    void* (*fl_init)(int job_threads);
    FlApp* (*app_create)(const FlAppSettings* settings);
    void (*app_destroy)(FlApp* app);
    bool (*app_tick)(FlApp* app, void* user_data, FlAppFrameFn frame);
    bool (*app_read_pixels)(FlApp* app, FlPixelReadback* out);
    void (*layout_start_auto)(void);
    void (*layout_configure)(FlLayoutConfig config);
    void (*layout_end)(void);
} SharedLibraryApi;

static SharedLibraryApi s_api;

#if defined(_WIN32)
typedef HMODULE SharedLibraryHandle;

static SharedLibraryHandle shared_library_open(const char* path) {
    return LoadLibraryA(path);
}

static void* shared_library_symbol(SharedLibraryHandle library, const char* name) {
    return (void*)(uintptr_t)GetProcAddress(library, name);
}

static void shared_library_close(SharedLibraryHandle library) {
    FreeLibrary(library);
}
#else
typedef void* SharedLibraryHandle;

static SharedLibraryHandle shared_library_open(const char* path) {
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}

static void* shared_library_symbol(SharedLibraryHandle library, const char* name) {
    return dlsym(library, name);
}

static void shared_library_close(SharedLibraryHandle library) {
    dlclose(library);
}
#endif

#define LOAD_SHARED_SYMBOL(library, field, name)                 \
    do {                                                         \
        void* symbol = shared_library_symbol((library), (name)); \
        if (symbol == NULL) {                                    \
            shared_library_close((library));                     \
            return 0;                                            \
        }                                                        \
        memcpy(&s_api.field, &symbol, sizeof(s_api.field));      \
    } while (0)

// Checks the caller wants run while the loaded library has a frame open (the frame-arena formatter probes). They are
// invoked from inside the frame callback because the state they read belongs to the flowi instance running the frame.
typedef struct InFrameProbes {
    int (*const* probes)(void);
    int count;
    int ok;
} InFrameProbes;

static void shared_library_smoke_frame(void* user_data) {
    InFrameProbes* in_frame = (InFrameProbes*)user_data;
    for (int i = 0; i < in_frame->count; i++) {
        if (!in_frame->probes[i]()) {
            in_frame->ok = 0;
        }
    }

    s_api.layout_start_auto();
    s_api.layout_configure((FlLayoutConfig) {
        .sizing = { .width = fl_sizing_grow(), .height = fl_sizing_grow() },
        .color = fl_color(24, 24, 32, 255),
        .child_alignment = { FlLayoutAlignmentX_Center, FlLayoutAlignmentY_Center },
    });
    s_api.layout_start_auto();
    s_api.layout_configure((FlLayoutConfig) {
        .sizing = { .width = fl_sizing_fixed(240), .height = fl_sizing_fixed(160) },
        .color = fl_color(200, 40, 50, 255),
    });
    s_api.layout_end();
    s_api.layout_end();
}

FLOWI_TEST_EXPORT int external_c_shared_library_smoke(const char* library_path, int (*const* in_frame_probes)(void),
                                                      int in_frame_probe_count) {
    SharedLibraryHandle library = shared_library_open(library_path);
    if (library == NULL) {
        return 0;
    }
    LOAD_SHARED_SYMBOL(library, fl_init, "fl_init");
    LOAD_SHARED_SYMBOL(library, app_create, "fl_app_create");
    LOAD_SHARED_SYMBOL(library, app_destroy, "fl_app_destroy");
    LOAD_SHARED_SYMBOL(library, app_tick, "fl_app_tick");
    LOAD_SHARED_SYMBOL(library, app_read_pixels, "fl_app_read_pixels");
    LOAD_SHARED_SYMBOL(library, layout_start_auto, "fl_layout_start_auto");
    LOAD_SHARED_SYMBOL(library, layout_configure, "fl_layout_configure");
    LOAD_SHARED_SYMBOL(library, layout_end, "fl_layout_end");

    // The bootstrap every consumer of the shared library runs first (the examples all open with it): it brings up
    // the job system, the arena tracker and the scratch arenas of the loaded library's core. Where the library
    // bundles its own core objects rather than sharing one with the executable, that core is otherwise
    // uninitialized and the first job the app schedules asserts. Each init is guarded, so a core already brought
    // up by the host is left as it is.
    s_api.fl_init(1);

    FlAppSettings settings = {
        .window_title = { .data = "flowi shared smoke", .length = sizeof("flowi shared smoke") - 1, .is_static = 1 },
        .width = 320,
        .height = 200,
        .headless = true,
    };
    FlApp* app = s_api.app_create(&settings);
    if (app == NULL) {
        shared_library_close(library);
        return 0;
    }

    InFrameProbes in_frame = { .probes = in_frame_probes, .count = in_frame_probe_count, .ok = 1 };
    int ok = s_api.app_tick(app, &in_frame, shared_library_smoke_frame);
    FlPixelReadback pixels = { 0 };
    ok = ok && in_frame.ok && s_api.app_read_pixels(app, &pixels) && pixels.pixels != NULL && pixels.width == 320
         && pixels.height == 200;
    s_api.app_destroy(app);
    shared_library_close(library);
    return ok;
}
