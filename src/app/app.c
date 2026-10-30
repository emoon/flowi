#include "app/app.h"

#include "app/app_gpu.h"
#include "app/host.h"
#include <platform/input_types.h>
#include <platform/window_backend.h>
#include <core/arena.h>
#include <core/assert.h>
#include <core/core.h>
#include <core/log.h>
#include <core/memory.h>
#include <core/os/os.h>
#include "ui/dock/dock_shared.h"
#include "ui/dock/dock_workspace.h"
#include <flowi/ui/cursor.h>
#include "ui/ui.h"
#include "ui/ui_private.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The two sentinels must carry the same value so a host position passes straight through, untranslated.
_Static_assert(FL_APP_WINDOW_POS_DEFAULT == FL_WINDOW_POS_DEFAULT,
               "FL_APP_WINDOW_POS_DEFAULT must equal the window backend's FL_WINDOW_POS_DEFAULT");

// Upper bound on live windows. Must not exceed the window backend's per-pump array bound.
#define FL_APP_MAX_WINDOWS 16

// One host-level window: a window-backend window + a render surface + a per-window UI context, plus the per-frame
// callback the tick loop calls for it. The main window (windows[0]) has a null frame - fl_app_tick supplies its
// callback - while a window opened with fl_app_window_open stores its own.
struct FlAppWindow {
    FlWindow* os_window; // window-backend handle (every live window has one)
    FlSurface* surface;  // this window's backend-owned render target
    FlUiCtx* ctx;        // this window's UI context (its own Clay tree, input, focus)
    FlAppFrameFn frame;  // per-window frame callback (null for the main window; tick supplies it)
    void* user_data;     // per-window callback user data
    bool is_main;        // closing the main window quits the app; closing any other closes just it

    // Optional close hook: consulted at the tick boundary when a close is seen; returning false vetoes it.
    // Its user data is independent of the per-frame callback's.
    FlAppCloseFn on_close;
    void* on_close_user_data;

    // True between a mid-frame fl_app_window_open and its materialization at the next tick boundary: the handle
    // exists and is returned to the caller, but its os_window/surface/ctx are null until then and it is not yet in
    // the window list. fl_app_window_close on a still-pending handle cancels the queued open.
    bool pending;

    // For a promoted dock window: the workspace this window hosts. Stashed by the default dock host on the
    // (queued) handle; app_attach_window binds it onto the context right after ui_create. Null for a plain window
    // (it keeps the registry default bound by ui_create).
    struct FlDockWorkspace* dock_workspace_to_bind;

    int width; // current render/layout pixel size (tracks the live window)
    int height;

    // Last render size pushed through ui_set_display_resolution (per window). Starts 0 so the first tick always
    // applies (a fresh UI context defaults to 1920x1080), then only re-applies on an actual pixel-size change.
    int applied_width;
    int applied_height;

    // Last absolute scale pushed to ui_set_scale for this window (content scale x ui_zoom). Starts at a sentinel
    // so the first compose always applies; the per-tick recompose only re-pushes when the product changes.
    float applied_scale;
};

struct FlApp {
    FlArena* arena;
    bool headless;

    // User UI-zoom preference (fl_app_set_ui_zoom), 1.0 = DPI-correct. App-wide; each window composes it against
    // its own monitor content scale into the absolute scale pushed to ui_set_scale.
    float ui_zoom;

    // The embedded flowi tier (app/host.h): render backend, shared resource tier, VFS + image-system singletons,
    // font cache, and the per-window surface + context bring-up.
    FlHost* host;

    // The render backend the GPU paths build against before the host exists (a windowed GPU backend needs its
    // SDL-GL surface first). Staging only: host_create adopts a copy and that copy is the live one from then on.
    // The GPU backend stays app-owned, so fl_app_destroy - not the host - calls its destroy.
    FlRenderBackend gpu_backend;
    bool adopted_gpu_backend;

    FlAppGpu* gpu; // non-null for headless GPU only: owns the EGL surfaceless context (windowed GPU's
                   // GL context + compositor live in the SDL window backend)

    // The window list. windows[0] is the main window; the rest are opened via fl_app_window_open.
    FlAppWindow* windows[FL_APP_MAX_WINDOWS];
    int window_count;

    // The main window's OS handle in the gap between create_window and app_attach_window appending it to
    // windows[] during fl_app_create. A failure in that gap bails to fl_app_destroy, whose teardown loop only
    // walks windows[] and so cannot see this handle. Cleared to null once attach takes ownership (the list owns
    // it then, so it is never double-destroyed) and never set again after create.
    FlWindow* pending_main_os_window;

    // Mid-frame open queue. A fl_app_window_open issued while a frame is emitting records its handle +
    // (copied) settings here instead of touching the window list; fl_app_tick materializes each at the boundary.
    struct FlPendingOpen {
        FlAppWindow* handle;       // the handle already returned to the caller
        FlWindowSettings settings; // copied at queue time (title bytes duplicated into the app arena)
    } pending_opens[FL_APP_MAX_WINDOWS];
    int pending_open_count;

    // The single OS-integration seam for every path - headless, windowed GPU, and windowed software. Drives each
    // window's lifecycle / pump / input / cursor / present.
    FlWindowBackend window_backend;

    u64 last_time_ns;
    bool have_last_time;

    // Deterministic delta-time override for headless timing tests: fl_app_tick feeds this fixed dt to
    // fl_platform_input_update / ui_update_end instead of sampling the wall clock.
    bool use_test_dt;
    float test_dt;

    // The main window's per-frame callback + user data captured each fl_app_tick. A promoted dock window reuses
    // the SAME frame fn - dock_workspace_current() gates which panels emit, so the one fn renders whichever
    // workspace the current window hosts.
    FlAppFrameFn main_frame;
    void* main_frame_user_data;
};

// A frame is active between fl_app_tick's ui_update_begin and ui_update_end. A file-static because fl_frame_str
// resolves its arena from the process-global UI context; v1 hosts a single app per process.
static bool s_frame_active;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Attach a host-level window to the shared tier: create its render surface and its UI context, wire the shared
// font cache + default font onto it, and append it to the window list. os_window must already exist - the GPU
// windowed path builds its render backend against the context before the surface does. The caller owns the
// FlAppWindow handle; attach only fills the fields it owns and clears pending, so a pre-set on_close survives
// materialization. Returns false (logging) on failure; appends on success.

static bool app_attach_window(FlApp* app, FlAppWindow* w, FlWindow* os_window, bool is_main, FlAppFrameFn frame,
                              void* user_data, int width, int height) {
    if (app->window_count >= FL_APP_MAX_WINDOWS) {
        log_error("fl_app: window list full (%d)", FL_APP_MAX_WINDOWS);
        return false;
    }

    w->os_window = os_window;
    w->is_main = is_main;
    w->frame = frame;
    w->user_data = user_data;
    w->width = width;
    w->height = height;
    w->pending = false;       // materialized: it now joins the live window list
    w->applied_scale = -1.0f; // sentinel: forces the first per-window scale compose to apply

    w->surface = host_surface_create(app->host, width, height);
    if (!w->surface) {
        return false;
    }

    w->ctx = host_window_attach(app->host, w->surface, width, height);
    if (!w->ctx) {
        // This window never joins app->windows, so the destroy loop can never reach it - free the surface here
        // or it leaks for the process lifetime.
        host_surface_destroy(app->host, w->surface);
        return false;
    }
    // A promoted dock window binds its own workspace here, the one safe point right after the context exists.
    if (w->dock_workspace_to_bind) {
        ui_ctx_set_dock_workspace(w->ctx, w->dock_workspace_to_bind);
    }

    app->windows[app->window_count++] = w;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
FlWindowConfig fl_app_main_window_config(const FlAppSettings* settings, const FlWindowBackend* backend) {
    float scale = backend->display_content_scale ? backend->display_content_scale(backend->user_data) : 1.0f;
    if (!(scale > 0.0f)) {
        scale = 1.0f;
    }
    return (FlWindowConfig) { .title = settings->window_title,
                              .width = (int)((float)settings->width * scale + 0.5f),
                              .height = (int)((float)settings->height * scale + 0.5f),
                              .x = FL_WINDOW_POS_DEFAULT,
                              .y = FL_WINDOW_POS_DEFAULT,
                              .fullscreen = settings->fullscreen,
                              .resizable = !settings->fixed_size };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Default dock host: the runner provides the OS-window mechanics for panel promotion; flowi owns the decision +
// lifecycle + demote-on-close. Registered once in fl_app_create. flowi never opens a window itself.

// flowi → host: on this window's close, tell flowi (which runs the demote + dissolves the second workspace) and
// let the close proceed. user_data is the FlAppWindow* host handle (the demote key).
static bool app_dock_on_close(void* user_data) {
    fl_dock_notify_window_closed(user_data);
    return true;
}

// flowi → host: open an OS window hosting the promoted workspace ws. Reuses the main window's frame fn, lets the
// compositor place the window, stashes ws so materialization binds it, and wires demote-on-close. Returns the
// FlAppWindow* as the opaque host handle. Called from inside a frame (the docking flush), so the open is QUEUED
// and materialized at the next tick boundary.
static void* app_dock_open_promoted_window(void* ud, struct FlDockWorkspace* ws, f32 w, f32 h) {
    FlApp* app = (FlApp*)ud;
    if (!app->main_frame) {
        log_error("fl_app: cannot promote — no frame callback captured yet (promote from inside a frame)");
        return nullptr;
    }
    FlWindowSettings settings = { .title = FL_S("flowi — promoted panel"),
                                  .width = (int)w,
                                  .height = (int)h,
                                  .x = FL_APP_WINDOW_POS_DEFAULT, // compositor-placed
                                  .y = FL_APP_WINDOW_POS_DEFAULT };
    FlAppWindow* win = fl_app_window_open(app, &settings, app->main_frame, app->main_frame_user_data);
    if (!win) {
        return nullptr;
    }
    win->dock_workspace_to_bind = ws; // bound onto the context at materialization (app_attach_window)
    fl_app_window_set_on_close(app, win, app_dock_on_close, win);
    return win;
}

// flowi → host: the flowi-initiated teardown path (a programmatic close). The user/WM-initiated path runs through
// app_dock_on_close instead.
static void app_dock_close_promoted_window(void* ud, void* host_window) {
    fl_app_window_close((FlApp*)ud, (FlAppWindow*)host_window);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Every fl_app_create failure path bails identically: tear down whatever came up (fl_app_destroy null-checks every
// subsystem, so it is safe at any point in the bring-up) and return null. Two macros - not helpers - so the
// log_error reports its own call-site line: FL_APP_FAIL for a failure that owns its message, FL_APP_BAIL for one
// whose callee already logged.
#define FL_APP_FAIL(app, cond, msg) \
    do {                            \
        if (cond) {                 \
            log_error(msg);         \
            fl_app_destroy(app);    \
            return nullptr;         \
        }                           \
    } while (0)
#define FL_APP_BAIL(app, cond)   \
    do {                         \
        if (cond) {              \
            fl_app_destroy(app); \
            return nullptr;      \
        }                        \
    } while (0)

// Create the main window's OS handle from the already-assigned window_backend at the requested size + title. The
// SDL paths must probe create_window first (a failed backend init leaves it null); the headless backend is always
// initialized, so it passes check_init=false.
static FlWindow* app_create_main_window(FlApp* app, const FlAppSettings* settings, bool check_init,
                                        const char* init_err, const char* window_err) {
    if (check_init && !app->window_backend.create_window) {
        log_error("%s", init_err);
        return nullptr;
    }
    FlWindowConfig window_cfg = fl_app_main_window_config(settings, &app->window_backend);
    FlWindow* win = app->window_backend.create_window(app->window_backend.user_data, &window_cfg);
    if (!win) {
        log_error("%s", window_err);
        return nullptr;
    }
    // Recorded so a bail between here and a successful attach still releases the platform window (the teardown
    // loop only walks windows[]); fl_app_create clears it once attach succeeds.
    app->pending_main_os_window = win;
    return win;
}

FlApp* fl_app_create(const FlAppSettings* settings) {
    FL_ASSERT(settings != nullptr);
    FL_ASSERT_MSG(settings->width > 0 && settings->height > 0, "fl_app requires a positive window size");

    // No-op when the host already brought the core services up with its own job_threads count.
    fl_init(1);

    FlArena* arena = arena_new();
    FlApp* app = arena_alloc(arena, FlApp);
    *app = (FlApp) { 0 };
    app->arena = arena;
    app->headless = settings->headless;
    app->ui_zoom = 1.0f; // DPI-correct until the user calls fl_app_set_ui_zoom

    // Resolve the effective renderer. On APPLE the software windowed backend is not built: windowed + software
    // maps to Metal, headless keeps the software default. FlAppRenderer_Gpu and _Metal both name the GPU
    // compute backend - one GL, one Metal - so both take the GPU path (create_gpu_renderer picks by build).
    FlAppRenderer renderer = settings->renderer;
#ifdef __APPLE__
    if (!settings->headless && renderer == FlAppRenderer_Software) {
        renderer = FlAppRenderer_Metal;
    }
#endif
    const bool use_gpu = (renderer == FlAppRenderer_Gpu || renderer == FlAppRenderer_Metal);

    // The main window's OS handle. The windowed-GPU path creates it early (the render backend builds against its
    // GL/Metal surface); the headless and windowed-software paths create it after the globals. Every path has one.
    FlWindow* main_os_window = nullptr;

    if (use_gpu) {
#if FL_APP_HAVE_GPU
        if (app->headless) {
            // fl_app_gpu owns the EGL surfaceless context and builds the backend against it.
            app->gpu = fl_app_gpu_create_headless(arena, &app->gpu_backend);
            FL_APP_FAIL(app, !app->gpu || app->gpu_backend.render == NULL,
                        "fl_app: GPU renderer initialization failed");
            app->adopted_gpu_backend = true;
        } else {
#if FL_APP_HAVE_SDL
            // Windowed GPU: the SDL WindowBackend owns the OS windows + the shared compositor. Create the backend
            // + the main window first so the GPU render backend can be built against its context. APPLE uses the
            // Metal windowed mode (CAMetalLayer compositor); every other platform uses SDL-GL.
#ifdef __APPLE__
            app->window_backend = sdl_window_backend_create(arena, FlWindowBackendMode_Metal);
#else
            app->window_backend = sdl_window_backend_create(arena, FlWindowBackendMode_Gl);
#endif
            main_os_window = app_create_main_window(app, settings, /*check_init*/ true,
                                                    "fl_app: SDL window backend initialization failed",
                                                    "fl_app: SDL window creation failed");
            FL_APP_BAIL(app, !main_os_window);
            FL_APP_BAIL(app, !fl_app_gpu_create_backend(arena, &app->gpu_backend));
            app->adopted_gpu_backend = true;
#else
            FL_APP_FAIL(app, true, "fl_app: windowed GPU mode requires the SDL host (build with FLOWI_SDL_BACKEND)");
#endif
        }
#else
        FL_APP_FAIL(app, true,
                    "fl_app: GPU renderer requires the EGL/GL build (OpenGL_EGL_FOUND -> FL_APP_HAVE_GPU); "
                    "use FlAppRenderer_Software or enable the GPU build");
#endif
    }

    // The embedded tier: shared resource tier, VFS + image system, font cache, and the hidden working-directory
    // asset mount that lets Font::load / Image::load resolve plain filesystem paths. The software path passes no
    // backend and lets the host build the renderer; the GPU paths hand over the backend they had to build against
    // their own surface. No render size: fl_app opens a window per OS window itself.
    FlHostSettings host_settings = { .mount_working_directory = true };
    app->host = host_create(arena, &host_settings, app->adopted_gpu_backend ? &app->gpu_backend : nullptr);
    FL_APP_BAIL(app, !app->host);

    if (app->headless) {
        // The headless window backend supplies a fixed drawable size + neutral content scale, the injected-event
        // pump, and cursor / hidden-drag recording. Present is a no-op (the GPU reads back from its offscreen
        // RGBA8 target instead).
        app->window_backend = headless_window_backend_create(arena);
        main_os_window = app_create_main_window(app, settings, /*check_init*/ false, nullptr,
                                                "fl_app: headless window creation failed");
        FL_APP_BAIL(app, !main_os_window);
        log_info("fl_app: headless harness ready (mouse/wheel/key/text/window-close injection; gamepad/axis deferred)");
    } else if (!use_gpu) {
#if FL_APP_HAVE_SDL
        // Windowed software: the SDL WindowBackend in software mode owns the OS window and presents the software
        // renderer's CPU pixel buffer. (Windowed GPU created its GL-mode backend + window above.)
        app->window_backend = sdl_window_backend_create(arena, FlWindowBackendMode_Software);
        main_os_window = app_create_main_window(app, settings, /*check_init*/ true,
                                                "fl_app: SDL software window backend initialization failed",
                                                "fl_app: SDL software window creation failed");
        FL_APP_BAIL(app, !main_os_window);
#else
        FL_APP_FAIL(app, true,
                    "fl_app: windowed mode requires the SDL host (build with FLOWI_SDL_BACKEND); "
                    "use headless=true or enable the bindings build");
#endif
    }

    // Attach the main window (windows[0]). Its frame callback is supplied per-tick by fl_app_tick (null here).
    FlAppWindow* main_win = arena_alloc(app->arena, FlAppWindow);
    *main_win = (FlAppWindow) { 0 };
    FL_APP_BAIL(app, !app_attach_window(app, main_win, main_os_window, /*is_main*/ true, nullptr, nullptr,
                                        settings->width, settings->height));
    // windows[] now owns the OS handle; drop the pending reference so teardown never destroys it twice.
    app->pending_main_os_window = nullptr;

    // Register the default dock host. The main window's context is current now (ui_create set g_ui_ctx), so the
    // host lands in the shared tier's dock state.
    fl_dock_register_host((FlDockHost) { .user_data = app,
                                         .open_promoted_window = app_dock_open_promoted_window,
                                         .close_promoted_window = app_dock_close_promoted_window });
    return app;
}

// Create the OS window + surface + context for a (non-main) window into the caller-provided handle. Shared by the
// immediate open path and the mid-frame deferred-open materialization. Returns false (logging, no leak) on failure.
static bool app_open_window_into(FlApp* app, FlAppWindow* w, const FlWindowSettings* settings, FlAppFrameFn frame,
                                 void* user_data) {
    FlWindowConfig window_cfg = { .title = settings->title,
                                  .width = settings->width,
                                  .height = settings->height,
                                  .x = settings->x,
                                  .y = settings->y,
                                  .resizable = true };
    FlWindow* os_window = app->window_backend.create_window(app->window_backend.user_data, &window_cfg);
    if (!os_window) {
        log_error("fl_app: window creation failed");
        return false;
    }
    if (!app_attach_window(app, w, os_window, /*is_main*/ false, frame, user_data, settings->width, settings->height)) {
        // Surface/context creation failed after the OS window came up; drop the OS window so it does not leak.
        if (app->window_backend.destroy_window) {
            app->window_backend.destroy_window(app->window_backend.user_data, os_window);
        }
        return false;
    }
    return true;
}

FlAppWindow* fl_app_window_open(FlApp* app, const FlWindowSettings* settings, FlAppFrameFn frame, void* user_data) {
    FL_ASSERT(app != nullptr && settings != nullptr);
    FL_ASSERT_MSG(settings->width > 0 && settings->height > 0, "fl_app_window_open requires a positive window size");

    if (!app->window_backend.create_window) {
        log_error("fl_app: multi-window requires an initialized window backend");
        return nullptr;
    }
    // Bound the live + queued windows against the list capacity so a materialization at the boundary can never
    // overflow windows[].
    if (app->window_count + app->pending_open_count >= FL_APP_MAX_WINDOWS) {
        log_error("fl_app: cannot open another window (list full at %d)", FL_APP_MAX_WINDOWS);
        return nullptr;
    }

    FlAppWindow* w = arena_alloc(app->arena, FlAppWindow);
    *w = (FlAppWindow) { 0 };

    // Mid-frame open: creating the window now would mutate the window list and swap the current context out from
    // under the emitting window. Queue the handle + a copy of the settings (the caller's title bytes need not
    // outlive this call) and materialize it at the next tick boundary.
    if (s_frame_active) {
        FlWindowSettings copy = *settings;
        copy.title = fl_string_copy(app->arena, settings->title);
        w->pending = true;
        w->frame = frame;
        w->user_data = user_data;
        app->pending_opens[app->pending_open_count].handle = w;
        app->pending_opens[app->pending_open_count].settings = copy;
        app->pending_open_count++;
        return w;
    }

    // Between-frame open: create the window immediately.
    if (!app_open_window_into(app, w, settings, frame, user_data)) {
        return nullptr;
    }
    return w;
}

// Materialize every window queued mid-frame by fl_app_window_open. Runs at the tick boundary, where mutating the
// window list and (re)making a context current is safe. A failure leaves the handle inert (ctx/os_window null, not
// in the list); the caller's pointer stays valid but dead.
static void app_apply_pending_opens(FlApp* app) {
    for (int i = 0; i < app->pending_open_count; i++) {
        FlAppWindow* w = app->pending_opens[i].handle;
        if (!app_open_window_into(app, w, &app->pending_opens[i].settings, w->frame, w->user_data)) {
            w->pending = false; // left inert: never entered the window list
            log_error("fl_app: deferred window open failed");
        }
    }
    app->pending_open_count = 0;
}

void fl_app_destroy(FlApp* app) {
    if (!app) {
        return;
    }

    // Teardown order: every window's context, then the host (asset mount, shared tier, surfaces, render backend,
    // the process-global VFS and image-system singletons it owns), then every window's OS handle. The GPU render
    // backend frees its GL objects while a window - and so the shared GL context - is still alive; the OS windows
    // go last, the final one tearing the shared GL context down.
    for (int i = 0; i < app->window_count; i++) {
        if (app->windows[i]->ctx) {
            host_window_detach(app->host, app->windows[i]->ctx);
        }
    }
    // The shared GL context must be current against a live window before any render-backend GL teardown (the
    // host's destroy_surface + destroy, both of which delete GL objects). The headless backend owns no GL context
    // here - its render context is the EGL one torn down below - so it leaves make_current null and this loop is
    // skipped.
    if (app->window_backend.make_current) {
        for (int i = 0; i < app->window_count; i++) {
            if (app->windows[i]->os_window) {
                app->window_backend.make_current(app->window_backend.user_data, app->windows[i]->os_window);
                break;
            }
        }
    }
    // The host reclaims any surface still outstanding in the right order (after the shared tier, before the
    // backend), so the window loop above does not have to.
    host_destroy(app->host);
    app->host = nullptr;
    // A GPU backend is app-owned - the host only borrowed it - so its destroy runs here, still with a live GL
    // context.
    if (app->adopted_gpu_backend && app->gpu_backend.destroy) {
        app->gpu_backend.destroy(app->gpu_backend.user_data);
    }
#if FL_APP_HAVE_GPU
    // After the backend frees its GL resources (context still current), tear down the headless EGL context.
    if (app->gpu) {
        fl_app_gpu_destroy(app->gpu);
    }
#endif
    // Windows are torn down before the backend that created them. The window backend's last destroy_window tears
    // down its shared GL state.
    if (app->window_backend.destroy_window) {
        for (int i = 0; i < app->window_count; i++) {
            if (app->windows[i]->os_window) {
                app->window_backend.destroy_window(app->window_backend.user_data, app->windows[i]->os_window);
            }
        }
        // A main window created but not yet appended to windows[] when fl_app_create bailed lives only here - the
        // loop above cannot reach it. Null on the success path, so this is then a no-op.
        if (app->pending_main_os_window) {
            app->window_backend.destroy_window(app->window_backend.user_data, app->pending_main_os_window);
        }
    }
    if (app->window_backend.destroy) {
        app->window_backend.destroy(app->window_backend.user_data);
    }

    fl_arena_destroy(app->arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Frame loop. Ordering within a tick: pump -> fl_platform_input_update -> begin -> frame -> end -> present.
// fl_platform_input_update is the edge snapshot - it derives mouse_pressed_this_frame / mouse_released_this_frame
// from the buttons_pressed / buttons_released flags the pump just set, then clears those raw flags. It MUST run
// after the pump and before the frame body, so an edge pumped this tick is visible to this tick's widgets and to
// ui_update_end's hot/active transition.
//
// fl_app calls it directly, exactly once per frame, and must NOT route through ui_frame_end_and_update_input:
// that helper calls fl_platform_input_update itself, double-updating the input deltas.

static void fl_app_pump_events(FlApp* app) {
    // Pump the shared OS queue ONCE and demux it across every live window by id. Build the parallel
    // handle/context arrays the seam matches against.
    if (app->window_backend.pump_events) {
        FlWindow* os_windows[FL_APP_MAX_WINDOWS];
        FlUiCtx* contexts[FL_APP_MAX_WINDOWS];
        for (int i = 0; i < app->window_count; i++) {
            os_windows[i] = app->windows[i]->os_window;
            contexts[i] = app->windows[i]->ctx;
        }
        app->window_backend.pump_events(app->window_backend.user_data, os_windows, contexts, app->window_count);
    }
}

// Tear a single host-level window down: its context, then its surface, then its OS handle (mirrors the shared
// teardown order), leaving the shared tier + backend alive. The window backend's destroy_window only reclaims its
// shared GL state on the LAST window.
static void app_window_destroy(FlApp* app, FlAppWindow* w) {
    if (w->ctx) {
        host_window_detach(app->host, w->ctx);
        w->ctx = nullptr;
    }
    if (w->surface) {
        host_surface_destroy(app->host, w->surface);
        w->surface = nullptr;
    }
    if (w->os_window && app->window_backend.destroy_window) {
        app->window_backend.destroy_window(app->window_backend.user_data, w->os_window);
        w->os_window = nullptr;
    }
}

// Resolve a window's pending close through its on_close hook, at the tick boundary. Returns true to proceed with
// the close (no hook, or the hook approved); returns false to veto - and clears the sticky quit_requested flag so
// the window keeps running and a later tick does not re-trigger the close.
static bool app_window_close_approved(FlAppWindow* w) {
    if (!w->ctx->input_state.quit_requested) {
        return false; // no close pending on this window
    }
    if (w->on_close && !w->on_close(w->on_close_user_data)) {
        w->ctx->input_state.quit_requested = false; // vetoed: clear and keep running
        return false;
    }
    return true;
}

// Close every non-main window whose close was seen this tick and not vetoed by its on_close hook, compacting the
// list. windows[0] (main) is never removed here - its close is the app quit, handled in fl_app_tick. Runs after
// the pump and before the render loop so a just-closed window is not rendered or presented this tick.
static void app_close_requested_secondaries(FlApp* app) {
    int keep = 1; // windows[0] (main) always stays
    for (int i = 1; i < app->window_count; i++) {
        FlAppWindow* w = app->windows[i];
        if (app_window_close_approved(w)) {
            app_window_destroy(app, w);
        } else {
            app->windows[keep++] = w;
        }
    }
    app->window_count = keep;
}

// Live window-pixel size for this window, written into *w/*h: a fixed offscreen size headless, the live SDL
// drawable size windowed.
static void fl_app_query_window_size(FlApp* app, FlAppWindow* win, int* w, int* h) {
    if (win->os_window) {
        app->window_backend.drawable_size(app->window_backend.user_data, win->os_window, w, h);
    }
}

// Monitor content scale (physical px / logical px) for this window, the DPI half of the composed UI scale:
// DPI-neutral 1.0 headless, the SDL drawable/window ratio windowed. A missing/non-positive value is 1.0.
static float fl_app_query_content_scale(FlApp* app, FlAppWindow* win) {
    if (win->os_window) {
        float scale = app->window_backend.content_scale(app->window_backend.user_data, win->os_window);
        return scale > 0.0f ? scale : 1.0f;
    }
    return 1.0f;
}

// Compose this window's absolute UI scale (monitor content scale x user ui_zoom) and push it to the core when it
// changes. The window's context must already be current (ui_set_scale targets the current context).
static void fl_app_apply_window_scale(FlApp* app, FlAppWindow* win) {
    float scale = fl_app_query_content_scale(app, win) * app->ui_zoom;
    if (scale != win->applied_scale) {
        ui_set_scale(scale, scale);
        win->applied_scale = scale;
    }
}

// Present one window's rendered surface, selecting by whichever present slot the backend provides: windowed GPU
// composites the surface texture (present); windowed software blits the software renderer's CPU pixel buffer
// (present_software). Headless no-ops for both renderers (the readback IS the surface).
static void fl_app_window_present(FlApp* app, FlAppWindow* win) {
    if (app->headless) {
        return;
    }
#if FL_APP_HAVE_GPU
    // Windowed GPU: resolve the surface texture and let the backend composite it over the window + present. The
    // handle type is platform-conditional: a void* id<MTLTexture> on APPLE, a u32 GL name elsewhere.
    if (app->window_backend.present) {
#ifdef __APPLE__
        void* tex = nullptr;
#else
        u32 tex = 0;
#endif
        u32 tex_w = 0, tex_h = 0;
        if (fl_app_gpu_surface_texture(win->surface, &tex, &tex_w, &tex_h)) {
            app->window_backend.present(app->window_backend.user_data, win->os_window, tex, tex_w, tex_h);
        }
        return;
    }
#endif
    // Windowed software: read back the offscreen RGB24 buffer and let the backend blit it to the window.
    if (app->window_backend.present_software) {
        FlPixelReadback rb;
        if (host_read_pixels(app->host, win->surface, &rb)) {
            app->window_backend.present_software(app->window_backend.user_data, win->os_window, rb.pixels, rb.width,
                                                 rb.height, rb.stride);
        }
    }
}

// One window's slice of a tick: make its context current, track its live size, recompose its scale, snapshot
// input, run its frame callback, flush its cursor, and present it. dt is the one shared step sampled in fl_app_tick.
static void fl_app_tick_window(FlApp* app, FlAppWindow* win, float dt, FlAppFrameFn frame, void* user_data) {
    ui_make_current(win->ctx);

    // Track the live window-pixel size: a tiling WM or HiDPI display hands back a size that differs from the
    // requested size, and the user can resize the window. On the first tick (applied_* start at 0) and on any
    // change, push the true pixel size through ui_set_display_resolution - which resizes the render backend
    // (change_resolution) and updates the layout dimensions (layout area only, no scale side effect). The software
    // present recreates its streaming texture when the readback size changes.
    int win_w = win->width;
    int win_h = win->height;
    fl_app_query_window_size(app, win, &win_w, &win_h);
    if (win_w != win->applied_width || win_h != win->applied_height) {
        ui_set_display_resolution(win_w, win_h);
        win->width = win_w;
        win->height = win_h;
        win->applied_width = win_w;
        win->applied_height = win_h;
    }

    // Recompose every tick (dedup'd on the unchanged product): a resize can move the window to a different-DPI
    // monitor, and content scale tracks that.
    fl_app_apply_window_scale(app, win);

    // Edge snapshot: this tick's press/release edges must be derived before any widget code or hit testing reads
    // the flags.
    fl_platform_input_update(win->ctx, (double)dt); // exactly once per window per tick

    ui_update_begin(win->ctx);
    s_frame_active = true;
    if (frame) {
        frame(user_data);
    }
    s_frame_active = false;
    ui_update_end(win->ctx, dt);

    // Flush this window's per-frame resolved cursor shape + sticky visibility. Cursor state is per-window; the
    // relative-mouse grab and the warp-back are not, and are flushed once per tick in
    // fl_app_flush_pointer_capture. The core and platform FlCursor types are field-copied, not reinterpret-cast,
    // so the two need not share a layout.
    FlCursor requested = fl_cursor_get_requested();
    FlCursor fl_cursor = { .shape = (FlCursorShape)requested.shape };
    bool cursor_visible = fl_cursor_get_visible();

    if (win->os_window) {
        void* ud = app->window_backend.user_data;
        app->window_backend.set_cursor(ud, win->os_window, fl_cursor);
        app->window_backend.set_cursor_visible(ud, win->os_window, cursor_visible);
    }

    fl_app_window_present(app, win);
}

// Resolve and flush the app-global pointer-capture state once per tick, after every window has ticked. The
// relative-mouse grab and the one-shot warp-back are app-global OS pointer state, but a hidden drag is owned by a
// single window, so the grab must reflect whichever window currently holds one - never the last window ticked.
// Scan the windows for the drag owner (relative mode sticky-on for its whole drag) and for a pending warp-back
// (set on the frame a drag ends, when relative mode is already back off), then flush both once.
static void fl_app_flush_pointer_capture(FlApp* app) {
    bool relative = false;
    bool have_warp = false;
    FlAppWindow* warp_win = nullptr;
    float warp_x = 0.0f, warp_y = 0.0f;

    for (int i = 0; i < app->window_count; i++) {
        FlAppWindow* win = app->windows[i];
        ui_make_current(win->ctx);
        if (ui_get_relative_mouse_mode()) {
            relative = true;
        }
        float wx, wy;
        if (ui_take_pending_warp(&wx, &wy)) {
            have_warp = true;
            warp_win = win;
            warp_x = wx;
            warp_y = wy;
        }
    }

    // The grab is global OS state (SDL_SetRelativeMouseMode / the headless backend's single flag), so the window
    // argument is immaterial for the relative-mode flush; the warp targets the owning window's coordinates.
    if (app->window_count > 0 && app->windows[0]->os_window) {
        void* ud = app->window_backend.user_data;
        app->window_backend.set_relative_mouse_mode(ud, app->windows[0]->os_window, relative);
        if (have_warp) {
            app->window_backend.warp_mouse(ud, warp_win->os_window, warp_x, warp_y);
        }
    }
}

bool fl_app_tick(FlApp* app, void* user_data, FlAppFrameFn frame) {
    FL_ASSERT(app != nullptr);

    // Capture the main window's callback for this tick so a promoted dock window opened during it reuses the same
    // frame fn. Set before the per-window loop, where the docking flush may call the dock host's
    // open_promoted_window (which reads these).
    app->main_frame = frame;
    app->main_frame_user_data = user_data;

    fl_app_pump_events(app);

    // Materialize any window queued mid-frame by fl_app_window_open last tick.
    app_apply_pending_opens(app);

    // The main window's close is the app quit - unless its on_close hook vetoes it. Fed identically by both
    // backends (SDL SDL_QUIT/WINDOWEVENT_CLOSE, headless inject_window_close, both via
    // fl_platform_input_push_window_event).
    if (app_window_close_approved(app->windows[0])) {
        return false;
    }

    app_close_requested_secondaries(app);

    // One shared delta-time for the whole tick: every window advances time by the same step.
    u64 now = get_monotonic_time_ns_os();
    float dt = 0.0f; // first tick has no previous timestamp
    if (app->have_last_time) {
        dt = (float)((double)(now - app->last_time_ns) / 1.0e9);
    }
    app->last_time_ns = now;
    app->have_last_time = true;

    if (app->use_test_dt) {
        dt = app->test_dt;
    }

    // Clear the app-global duplicate-id tracker once here, before any window emits, so it accumulates every
    // window's ids this frame and catches cross-window id collisions. This is the boundary that spans all
    // windows; the per-window ui_update_begin no longer clears the tracker. Debug-only no-op.
    ui_app_frame_begin(app->windows[0]->ctx);

    // The main window's frame callback is the one passed to fl_app_tick; every other window uses the callback it
    // was opened with.
    for (int i = 0; i < app->window_count; i++) {
        FlAppWindow* win = app->windows[i];
        FlAppFrameFn win_frame = win->is_main ? frame : win->frame;
        void* win_user_data = win->is_main ? user_data : win->user_data;
        fl_app_tick_window(app, win, dt, win_frame, win_user_data);
    }

    // Every window has refreshed its records: retire the ones no window touched.
    ui_app_frame_end(app->windows[0]->ctx);

    fl_app_flush_pointer_capture(app);

    return true;
}

void fl_app_run(FlApp* app, void* user_data, FlAppFrameFn frame) {
    while (fl_app_tick(app, user_data, frame)) {}
}

void fl_app_window_close(FlApp* app, FlAppWindow* window) {
    FL_ASSERT(app != nullptr && window != nullptr);
    if (window->is_main) {
        // The main window's close is the app quit (let it reach fl_app_tick); fl_app_window_close cannot close it.
        log_warning("fl_app_window_close: ignoring close of the main window (close it to quit the app)");
        return;
    }
    // A window still queued by a mid-frame open has no context to mark - drop it from the open queue so it never
    // comes up.
    if (window->pending) {
        int keep = 0;
        for (int i = 0; i < app->pending_open_count; i++) {
            if (app->pending_opens[i].handle != window) {
                app->pending_opens[keep++] = app->pending_opens[i];
            }
        }
        app->pending_open_count = keep;
        window->pending = false;
        return;
    }
    if (!window->ctx) {
        return; // already closed (or a failed deferred open): nothing to mark
    }
    // Mark it for teardown at the start of the next tick - the same path a window-manager close takes - so it is
    // safe to call from inside a frame callback. Its on_close hook still runs and may veto.
    window->ctx->input_state.quit_requested = true;
}

void fl_app_window_set_on_close(FlApp* app, FlAppWindow* window, FlAppCloseFn on_close, void* user_data) {
    FL_ASSERT(app != nullptr && window != nullptr);
    window->on_close = on_close;
    window->on_close_user_data = user_data;
}

void fl_app_set_ui_zoom(FlApp* app, float zoom) {
    FL_ASSERT(app != nullptr);
    FL_ASSERT_MSG(zoom > 0.0f, "fl_app_set_ui_zoom requires a positive zoom multiplier");
    app->ui_zoom = zoom;
    // Recompose every window's scale right away, so the change is visible on the next frame even if no window
    // resizes. ui_set_scale targets the current context, so make each window current to push its scale, then
    // restore the context that was current on entry - the setter must not leave a different window selected.
    FlUiCtx* prev = ui_get_ctx();
    for (int i = 0; i < app->window_count; i++) {
        ui_make_current(app->windows[i]->ctx);
        fl_app_apply_window_scale(app, app->windows[i]);
    }
    if (prev) {
        ui_make_current(prev);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Headless harness surface. The fl_app_inject_* entry points drive the offscreen input backend, so they
// assert headless.

// Shared precondition for every headless-only entry point. One macro (not a helper) so a failing call reports its
// own line in the assert.
#define FL_APP_REQUIRE_HEADLESS(app) \
    FL_ASSERT_MSG((app) && (app)->headless, "this fl_app entry point requires headless mode")

// The headless harness pumps a per-window queue, so injection must name a window; these injectors target the main
// window.
static FlWindow* app_main_os_window(FlApp* app) {
    return app->windows[0]->os_window;
}

void fl_app_inject_mouse_move(FlApp* app, i32 x, i32 y) {
    FL_APP_REQUIRE_HEADLESS(app);
    window_backend_headless_inject_mouse_move(&app->window_backend, app_main_os_window(app), x, y);
}

void fl_app_inject_mouse_button(FlApp* app, FlMouseButton button, bool down) {
    FL_APP_REQUIRE_HEADLESS(app);
    window_backend_headless_inject_mouse_button(&app->window_backend, app_main_os_window(app), button, down);
}

// Inject a raw relative-motion delta (a hidden-drag scrub), folded into the core's raw-delta accumulator and read
// via ui_mouse_drag_delta() (scaled). Like the other inject_* calls it is observed on the following tick.
void fl_app_inject_mouse_delta(FlApp* app, f32 delta_x, f32 delta_y) {
    FL_APP_REQUIRE_HEADLESS(app);
    window_backend_headless_inject_mouse_delta(&app->window_backend, app_main_os_window(app), delta_x, delta_y);
}

// Queue a mouse-wheel delta (x/y), cached by fl_platform_input_update for the scoped ui_mouse_wheel_delta() read.
// Routed through the window backend, not straight to input state, so it is observed on the following tick.
void fl_app_inject_wheel(FlApp* app, f32 x, f32 y) {
    FL_APP_REQUIRE_HEADLESS(app);
    window_backend_headless_inject_wheel(&app->window_backend, app_main_os_window(app), x, y);
}

void fl_app_inject_key(FlApp* app, FlKey key, bool down) {
    FL_APP_REQUIRE_HEADLESS(app);
    window_backend_headless_inject_key(&app->window_backend, app_main_os_window(app), key, down);
}

void fl_app_inject_text(FlApp* app, FlString text) {
    FL_APP_REQUIRE_HEADLESS(app);
    window_backend_headless_inject_text(&app->window_backend, app_main_os_window(app), text);
}

void fl_app_inject_window_close(FlApp* app) {
    FL_APP_REQUIRE_HEADLESS(app);
    window_backend_headless_inject_window_close(&app->window_backend, app_main_os_window(app));
}

void fl_app_inject_drop_file(FlApp* app, FlString path, float x, float y) {
    FL_APP_REQUIRE_HEADLESS(app);
    window_backend_headless_inject_drop_file(&app->window_backend, app_main_os_window(app), path, x, y);
}

void fl_app_inject_window_close_for(FlApp* app, FlAppWindow* window) {
    FL_APP_REQUIRE_HEADLESS(app);
    FL_ASSERT(window != nullptr);
    window_backend_headless_inject_window_close(&app->window_backend, window->os_window);
}

void fl_app_inject_window_focus_for(FlApp* app, FlAppWindow* window, bool gained) {
    FL_APP_REQUIRE_HEADLESS(app);
    FL_ASSERT(window != nullptr);
    window_backend_headless_inject_window_focus(&app->window_backend, window->os_window, gained);
}

void fl_app_set_window_content_scale_for(FlApp* app, FlAppWindow* window, float scale) {
    FL_APP_REQUIRE_HEADLESS(app);
    FL_ASSERT(window != nullptr);
    window_backend_headless_set_content_scale(window->os_window, scale);
}

FlAppWindow* fl_app_main_window(FlApp* app) {
    return fl_app_window_at(app, 0);
}

int fl_app_window_count(FlApp* app) {
    FL_ASSERT(app != nullptr);
    return app->window_count;
}

FlAppWindow* fl_app_window_at(FlApp* app, int index) {
    FL_ASSERT(app != nullptr && index >= 0 && index < app->window_count);
    return app->windows[index];
}

FlCursorShape fl_app_headless_cursor_shape(FlApp* app) {
    FL_APP_REQUIRE_HEADLESS(app);
    // Field-copy the platform FlCursor read-back into the core's FlCursor, the inverse of the flush seam above.
    FlCursor fl_cursor = window_backend_headless_last_cursor(&app->window_backend);
    FlCursor cursor = { .shape = (FlCursorShape)fl_cursor.shape };
    return (u32)cursor.shape < FlCursorShape_Count ? cursor.shape : FlCursorShape_Arrow;
}

bool fl_app_headless_relative_mouse_mode(FlApp* app) {
    FL_APP_REQUIRE_HEADLESS(app);
    return window_backend_headless_relative_mouse_mode(&app->window_backend);
}

FlVec2 fl_app_headless_last_warp(FlApp* app) {
    FL_APP_REQUIRE_HEADLESS(app);
    return window_backend_headless_last_warp(&app->window_backend);
}

void fl_app_set_test_dt(FlApp* app, float dt) {
    FL_APP_REQUIRE_HEADLESS(app);
    app->use_test_dt = (dt > 0.0f);
    app->test_dt = dt;
}

bool fl_app_read_pixels(FlApp* app, FlPixelReadback* out) {
    FL_ASSERT(app != nullptr && out != nullptr);
    if (!app->host) {
        return false;
    }
    // The golden-comparison surface is the main window's.
    return host_read_pixels(app->host, app->windows[0]->surface, out);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_frame_str(const char* ptr, size_t len) {
    FlUiCtx* ctx = ui_get_ctx();
    // Fatal in every build type: the next statement allocates out of the frame arena this check just proved
    // absent.
    FL_ASSERT_FATAL_MSG(ctx != nullptr, "fl_frame_str has no active UI context");
    // The precondition is the frame arena being live, which the UI layer owns - not the fl_app window loop. A host
    // driving ui_update_begin/ui_update_end itself has a frame open with no fl_app frame callback on the stack.
    FL_ASSERT_FATAL_MSG(ctx->frame_open, "fl_frame_str called outside an active frame");

    char* copy = arena_alloc_array(ctx->render_frame_allocator, char, len);
    if (len > 0) {
        memory_copy(copy, len, ptr, len);
    }
    return (FlString) { .data = copy, .length = len, .is_static = 0, .is_ascii = 0 };
}

FlString* fl_frame_str_array(uint32_t count) {
    FlUiCtx* ctx = ui_get_ctx();
    // Fatal in every build type: the frame-arena allocation below has no null-context path.
    FL_ASSERT_FATAL_MSG(ctx != nullptr, "fl_frame_str_array has no active UI context");
    // Same precondition as fl_frame_str: the frame arena is live between ui_update_begin and ui_update_end.
    FL_ASSERT_FATAL_MSG(ctx->frame_open, "fl_frame_str_array called outside an active frame");

    if (count == 0) {
        return nullptr;
    }
    // Uninitialized on purpose - the caller writes every slot before handing the array on. The counted-array
    // widgets reject count == 0, so the null above never reaches one as a live options list.
    return arena_alloc_array(ctx->render_frame_allocator, FlString, count);
}
