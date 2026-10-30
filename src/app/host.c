#include "app/host.h"

#include <core/arena.h>
#include <core/arena_watchdog.h>
#include <core/assert.h>
#include <core/log.h>
#include <core/os/os.h>
#include <render/software/software_renderer.h>
#include "ui/font_cache.h"
#include "ui/image.h"
#include "ui/ui.h"
#include "ui/ui_private.h"
#include "vfs/vfs.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Upper bound on live surfaces: a host with this many render targets is already pathological.
#define FL_HOST_MAX_SURFACES 16

struct FlHost {
    FlArena* arena; // caller-owned: the host allocates from it and never destroys it

    FlRenderBackend backend;        // software (offscreen RGB24) or the caller's adopted GPU backend
    bool owns_backend;              // false when the backend was adopted, so destroy leaves it to its owner
    UiShared* shared;               // app-level shared resource tier
    struct FlFontCache* font_cache; // shared font cache, set on every window's context
    FlFontId default_font;          // built-in 8x8 default font, made active on every window's context
    FlVfsMount* asset_mount;        // hidden default filesystem mount for Font::load / Image::load (optional)

    // Whether THIS host initialized the process-global VFS / image-system singletons (first-caller-wins: both
    // are no-ops when someone else got there first). Destroy tears down only what it owns.
    bool owns_vfs;
    bool owns_image_system;

    // Surfaces handed out by host_surface_create and not yet destroyed, so host_destroy can reclaim any the
    // caller left behind at the right point in the teardown order (before the backend that created them).
    FlSurface* surfaces[FL_HOST_MAX_SURFACES];
    i32 surface_count;

    // The single window a sized create opened, and the whole of what the public fl_host_* surface acts on.
    // Null for fl_app, which asks for an unsized host and opens a window per OS window itself.
    FlSurface* window_surface;
    FlUiCtx* window_ctx;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Every fl_host_create failure path bails identically: tear down whatever came up (fl_host_destroy null-checks
// every subsystem, so it is safe at any point in the bring-up) and return null. A macro, not a helper, so the
// log_error reports its own call-site line.

#define FL_HOST_FAIL(host, cond, msg) \
    do {                              \
        if (cond) {                   \
            log_error(msg);           \
            host_destroy(host);       \
            return nullptr;           \
        }                             \
    } while (0)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction order: font cache -> render backend -> shared tier -> fl_vfs_init -> image_create_system ->
// (optional) asset mount. The font cache comes first so the software renderer can be built against it.

FlHost* host_create(FlArena* arena, const FlHostSettings* settings, const FlRenderBackend* adopt_backend) {
    FL_ASSERT(arena != nullptr);
    FL_ASSERT(settings != nullptr);

    FlHost* host = arena_alloc(arena, FlHost);
    *host = (FlHost) { 0 };
    host->arena = arena;

    // ui_update_begin() pumps the font cache, so it must be non-null even when a frame draws no text. The cache
    // and the built-in default font are shared across windows; the load scale multiplies the 8px base (2 -> 16px).
    host->font_cache = font_cache_new(arena);
    host->default_font = font_cache_load_default_font(host->font_cache, 2);

    if (adopt_backend) {
        host->backend = *adopt_backend;
        host->owns_backend = false;
    } else {
        host->backend = create_software_renderer(arena, host->font_cache, FontRenderMode_Grayscale);
        host->owns_backend = true;
    }
    FL_HOST_FAIL(host, host->backend.render == nullptr, "fl_host: no usable render backend");

    host->shared = ui_shared_create(arena, &host->backend);
    FL_HOST_FAIL(host, !host->shared, "fl_host: shared resource tier creation failed");

    // ui_update_begin() pumps the VFS and the image system, so both must be initialized before the first frame.
    host->owns_vfs = fl_vfs_init(arena);
    host->owns_image_system = image_create_system(arena, &host->backend);

    // The hidden default asset mount: Font::load / Image::load resolve plain filesystem paths (absolute, or
    // relative to the cwd) against it. On the main thread vfs_mount dispatches asynchronously, so wait for it
    // to finish before the first frame can issue a load.
    if (settings->mount_working_directory) {
        FlVfsMountResult asset_mount = vfs_mount(get_current_directory_os(arena));
        FL_HOST_FAIL(host, asset_mount.status != FlVfsMountErrorStatus_Success || asset_mount.mount == nullptr,
                     "fl_host: failed to mount working directory for assets");
        vfs_wait_all();
        host->asset_mount = asset_mount.mount;
        ui_set_asset_mount(host->asset_mount);
    }

    // A sized create opens the host's one window here; a multi-window caller (fl_app) asks for no size and
    // drives host_surface_create / host_window_attach per OS window instead.
    if (settings->render_width > 0 && settings->render_height > 0) {
        host->window_surface = host_surface_create(host, settings->render_width, settings->render_height);
        FL_HOST_FAIL(host, host->window_surface == nullptr, "fl_host: render surface creation failed");
        host->window_ctx
            = host_window_attach(host, host->window_surface, settings->render_width, settings->render_height);
        FL_HOST_FAIL(host, host->window_ctx == nullptr, "fl_host: UI context creation failed");
    }

    return host;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void host_destroy(FlHost* host) {
    if (!host) {
        return;
    }

    // The host's own window goes first - contexts are torn down before the shared tier they attach to. A
    // multi-window caller has no window here and has already detached its own.
    if (host->window_ctx) {
        ui_destroy(host->window_ctx);
        host->window_ctx = nullptr;
    }

    // Image loads still running on job workers reach the backend and the shared registry, which are destroyed
    // below, and read through the asset mount closed next. Stop and drain them first.
    if (host->owns_image_system) {
        image_stop_loading();
    }

    // The mount lives in the process-global VFS (its own arena), so close it explicitly rather than leaning on
    // the host arena.
    if (host->asset_mount) {
        ui_set_asset_mount(nullptr);
        fl_vfs_mount_close(host->asset_mount);
        host->asset_mount = nullptr;
    }

    // The caller has already detached its contexts by the time it gets here, and a GPU caller has made its
    // context current, since the teardown below deletes GL objects.
    if (host->shared) {
        ui_shared_destroy(host->shared);
        host->shared = nullptr;
    }
    // Surfaces are torn down after the contexts and the shared tier, and before the backend that created them.
    if (host->backend.destroy_surface) {
        for (i32 i = 0; i < host->surface_count; i++) {
            host->backend.destroy_surface(host->backend.user_data, host->surfaces[i]);
        }
    }
    host->surface_count = 0;
    if (host->owns_backend && host->backend.destroy) {
        host->backend.destroy(host->backend.user_data);
    }

    // The font cache goes after the backend, which may hold a glyph cache built from these faces, and before
    // vfs_destroy, because a face still loading owns a VFS handle that font_cache_destroy closes. Its FreeType
    // library and per-face arenas are heap-owned, so the caller's arena does not reclaim them.
    if (host->font_cache) {
        font_cache_destroy(host->font_cache);
        host->font_cache = nullptr;
    }

    // Reset the process-global singletons this create brought up (reverse of create order), so a later
    // fl_host_create in the same process re-initializes them instead of reusing pointers into an arena that
    // may since have been destroyed. Owner-gated: a pre-existing owner's globals are left alone.
    if (host->owns_image_system) {
        image_destroy_system();
        host->owns_image_system = false;
    }
    if (host->owns_vfs) {
        vfs_destroy();
        host->owns_vfs = false;
    }

    // The arena is the caller's; it frees the FlHost allocation along with everything else on it.
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Per-window surface + context.

FlSurface* host_surface_create(FlHost* host, i32 width, i32 height) {
    FL_ASSERT(host != nullptr);
    if (!host->backend.create_surface) {
        log_error("fl_host: render backend has no create_surface");
        return nullptr;
    }
    if (host->surface_count >= FL_HOST_MAX_SURFACES) {
        log_error("fl_host: surface list full (%d)", FL_HOST_MAX_SURFACES);
        return nullptr;
    }
    FlSurface* surface = host->backend.create_surface(host->backend.user_data, width, height);
    if (surface) {
        host->surfaces[host->surface_count++] = surface;
    }
    return surface;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void host_surface_destroy(FlHost* host, FlSurface* surface) {
    FL_ASSERT(host != nullptr);
    if (!surface) {
        return;
    }
    for (i32 i = 0; i < host->surface_count; i++) {
        if (host->surfaces[i] == surface) {
            host->surfaces[i] = host->surfaces[--host->surface_count];
            break;
        }
    }
    if (host->backend.destroy_surface) {
        host->backend.destroy_surface(host->backend.user_data, surface);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlUiCtx* host_window_attach(FlHost* host, FlSurface* surface, i32 width, i32 height) {
    FL_ASSERT(host != nullptr);
    FL_ASSERT(surface != nullptr);

    FlUiCtx* ctx = ui_create(host->arena, host->shared);
    if (!ctx) {
        log_error("fl_host: UI context creation failed");
        return nullptr;
    }

    ui_set_surface(ctx, surface);
    ui_set_font_cache(ctx, host->font_cache);
    // ui_font_set and the display resolution target the current context, so make this one current first.
    ui_make_current(ctx);
    ui_font_set(host->default_font);
    ui_set_display_resolution(width, height);
    return ctx;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void host_window_detach(FlHost* host, FlUiCtx* ctx) {
    FL_ASSERT(host != nullptr);
    if (ctx) {
        ui_destroy(ctx);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool host_read_pixels(FlHost* host, FlSurface* surface, FlPixelReadback* out) {
    FL_ASSERT(host != nullptr);
    // A host created without a size never opened a window surface. Backends dereference the handle, so
    // answering false here keeps that a failed read rather than a crash.
    if (!surface || !host->backend.read_pixels) {
        return false;
    }
    return host->backend.read_pixels(host->backend.user_data, surface, out);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The public `fl_host_*` surface: thin forwarders onto the internal entry points above. flowi's own code never
// calls these - see the note in app/host.h for why an intra-library call to an exported name is a real bug.

FlHost* fl_host_create(FlArena* arena, const FlHostSettings* settings) {
    return host_create(arena, settings, nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_host_destroy(FlHost* host) {
    host_destroy(host);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// One call per frame. The first opens the host's frame; every one after it closes the frame the caller just
// emitted, pumps delta_time seconds of input into the next, and opens that.

void fl_host_frame_update(FlHost* host, double delta_time) {
    FL_ASSERT(host != nullptr);
    FL_ASSERT(host->window_ctx != nullptr);

    if (host->window_ctx->frame_open) {
        ui_frame_end_and_update_input(host->window_ctx, delta_time);
    } else {
        ui_update_begin(host->window_ctx);
    }

    arena_watchdog_update();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_host_read_pixels(FlHost* host, FlPixelReadback* out) {
    FL_ASSERT(host != nullptr);
    return host_read_pixels(host, host->window_surface, out);
}
