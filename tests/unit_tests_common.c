// Shared fixtures used by more than one unit_tests translation unit.

#include "unit_tests_common.h"

FlArena* s_arena;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// arena: reservation lifecycle - fl_arena_destroy must release its VMA, not merely decommit

#if defined(__linux__)
u64 count_proc_self_maps(void) {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) {
        return 0;
    }
    u64 lines = 0;
    int c;
    int prev = '\n';
    while ((c = fgetc(f)) != EOF) {
        if (c == '\n') {
            lines++;
        }
        prev = c;
    }
    if (prev != '\n') {
        lines++; // final line without trailing newline
    }
    fclose(f);
    return lines;
}
#endif

const uint8_t k_stub_vfs_data[] = { 'h', 'e', 'l', 'l', 'o', ' ', 'v', 'f', 's' };

FlVfsDriverError stub_vfs_ok(void) {
    return (FlVfsDriverError) { .vfs_status = FlVfsStatus_Success };
}

FlVfsDriverError stub_vfs_not_found(void) {
    return (FlVfsDriverError) { .vfs_status = FlVfsStatus_FileNotFound };
}

// Balance counters: every plugin handle the driver hands out must come back through close.
// vfs.destroy_closes_leaked_handles_across_cycles diffs these around a create/destroy cycle.
_Atomic int s_stub_vfs_alloc_count;

_Atomic int s_stub_vfs_close_count;
_Atomic int s_stub_vfs_read_count; // Reads that reached the driver

// When set, a parent-less probe of the file resolves to a file handle (default: parent-less probe
// is the mount-root open and yields a directory). Lets a test drive the resolver's file-read
// branch directly. When set, read reports one byte short of what get_size promised, forcing the
// resolver's short-read error branch.
bool s_stub_vfs_open_as_file;

bool s_stub_vfs_short_read;

bool s_stub_vfs_oversize;

// Optional encoded payload used by mounted-image tests. Null keeps the ordinary VFS fixture's text payload.
const u8* s_stub_vfs_override_data;

size_t s_stub_vfs_override_size;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const u8* stub_vfs_data(size_t* size) {
    if (s_stub_vfs_override_data) {
        *size = s_stub_vfs_override_size;
        return s_stub_vfs_override_data;
    }
    *size = sizeof(k_stub_vfs_data);
    return k_stub_vfs_data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

StubVfsHandle* stub_vfs_alloc(bool is_dir) {
    StubVfsHandle* h = (StubVfsHandle*)malloc(sizeof(StubVfsHandle));
    if (h) {
        h->is_dir = is_dir;
        h->offset = 0;
        s_stub_vfs_alloc_count++;
    }
    return h;
}

// Match the single file both relative to the mount root ("hello.txt") and absolute ("/hello.txt").
bool stub_vfs_is_file(FlString path) {
    const int n = (int)sizeof(STUB_VFS_FILE) - 1;
    if (path.length == n && memcmp(path.data, STUB_VFS_FILE, (size_t)n) == 0) {
        return true;
    }
    if (path.length == n + 1 && path.data[0] == '/' && memcmp(path.data + 1, STUB_VFS_FILE, (size_t)n) == 0) {
        return true;
    }
    return false;
}

// Match the mount sources the cases use ("/stub", "/stub-image-a", ...), with or without the leading slash.
// The bare root ("" / "/") matches too: a case that drives the resolver by hand opens it directly.
bool stub_vfs_is_stub_root(FlString path) {
    FlString name = path;
    if (name.length > 0 && name.data[0] == '/') {
        name.data++;
        name.length--;
    }
    if (name.length == 0) {
        return true;
    }
    return name.length >= 4 && memcmp(name.data, "stub", 4) == 0;
}

bool stub_vfs_supports_path(FlString path) {
    (void)path;
    return true;
}

bool stub_vfs_can_open(FlString path) {
    (void)path;
    return true; // the only registered driver - accept the mount source
}

FlVfsOpenResult stub_vfs_open(void* instance, void* parent_handle, FlString path, uint32_t flags,
                              FlVfsMountOptions mount_options) {
    (void)instance;
    (void)flags;
    (void)mount_options;

    // parent_handle == NULL is the mount-root open: hand back the root directory. When the
    // open-as-file toggle is set, a parent-less probe of the file yields a file handle instead,
    // so a test can drive the resolver's file-read branch.
    //
    // Only the /stub* sources the cases mount are claimed: driver resolution tries registered drivers before
    // the built-in LocalFS fallback, so a broader claim here would shadow it for every path.
    if (parent_handle == NULL) {
        if (!stub_vfs_is_stub_root(path) && !stub_vfs_is_file(path)) {
            return (FlVfsOpenResult) { .handle = NULL, .error = stub_vfs_not_found() };
        }
        if (s_stub_vfs_open_as_file && stub_vfs_is_file(path)) {
            StubVfsHandle* h = stub_vfs_alloc(false);
            return (FlVfsOpenResult) { .handle = h,
                                       .is_directory = false,
                                       .error = h ? stub_vfs_ok() : stub_vfs_not_found() };
        }
        StubVfsHandle* h = stub_vfs_alloc(true);
        return (
            FlVfsOpenResult) { .handle = h, .is_directory = true, .error = h ? stub_vfs_ok() : stub_vfs_not_found() };
    }
    if (stub_vfs_is_file(path)) {
        StubVfsHandle* h = stub_vfs_alloc(false);
        return (
            FlVfsOpenResult) { .handle = h, .is_directory = false, .error = h ? stub_vfs_ok() : stub_vfs_not_found() };
    }
    return (FlVfsOpenResult) { .handle = NULL, .error = stub_vfs_not_found() };
}

bool stub_vfs_is_directory(void* instance, void* handle) {
    (void)instance;
    StubVfsHandle* h = (StubVfsHandle*)handle;
    return h && h->is_dir;
}

FlVfsSizeResult stub_vfs_get_size(void* instance, void* handle, FlString relative_path) {
    (void)instance;
    StubVfsHandle* h = (StubVfsHandle*)handle;
    if (!h) {
        return (FlVfsSizeResult) { .size = -1, .error = stub_vfs_not_found() };
    }
    // Empty path: size of the already-open file.
    if (relative_path.length == 0) {
        if (h->is_dir) {
            return (FlVfsSizeResult) { .size = -1, .error = stub_vfs_not_found() };
        }
        if (s_stub_vfs_oversize) {
            return (FlVfsSizeResult) { .size = STUB_VFS_OVERSIZE, .error = stub_vfs_ok() };
        }
        size_t data_size = 0;
        (void)stub_vfs_data(&data_size);
        return (FlVfsSizeResult) { .size = (int64_t)data_size, .error = stub_vfs_ok() };
    }
    // Non-empty path: a size/existence probe under this handle. Only our file resolves.
    if (stub_vfs_is_file(relative_path)) {
        size_t data_size = 0;
        (void)stub_vfs_data(&data_size);
        return (FlVfsSizeResult) { .size = (int64_t)data_size, .error = stub_vfs_ok() };
    }
    return (FlVfsSizeResult) { .size = -1, .error = stub_vfs_not_found() };
}

// Opt-in gate, off unless a case arms it: see the declaration in unit_tests_common.h. A gated read gives up after
// the timeout, so a case that fails while holding it cannot hang the run.
#define STUB_VFS_GATE_TIMEOUT_NS 20000000000ull

_Atomic bool s_stub_vfs_read_gate_armed;
_Atomic bool s_stub_vfs_read_entered;
_Atomic bool s_stub_vfs_read_release;

FlVfsReadResult stub_vfs_read(void* instance, void* handle, FlString relative_path, uint8_t* buffer, int64_t size) {
    (void)instance;
    (void)relative_path;
    atomic_fetch_add(&s_stub_vfs_read_count, 1);
    if (atomic_load(&s_stub_vfs_read_gate_armed)) {
        atomic_store(&s_stub_vfs_read_entered, true);
        const u64 deadline_ns = get_monotonic_time_ns_os() + STUB_VFS_GATE_TIMEOUT_NS;
        while (!atomic_load(&s_stub_vfs_read_release) && get_monotonic_time_ns_os() < deadline_ns) {
            sleep_ms(1);
        }
    }
    StubVfsHandle* h = (StubVfsHandle*)handle;
    if (!h || h->is_dir || !buffer || size < 0) {
        return (FlVfsReadResult) { .bytes_read = -1, .error = stub_vfs_not_found() };
    }
    size_t data_size = 0;
    const u8* data = stub_vfs_data(&data_size);
    int64_t remaining = (int64_t)data_size - h->offset;
    if (remaining < 0) {
        remaining = 0;
    }
    int64_t n = size < remaining ? size : remaining;
    if (n > 0) {
        memcpy(buffer, data + h->offset, (size_t)n);
        h->offset += n;
    }
    // Report one byte short of what was actually delivered so callers see a short read.
    int64_t reported = (s_stub_vfs_short_read && n > 0) ? n - 1 : n;
    return (FlVfsReadResult) { .bytes_read = reported, .error = stub_vfs_ok() };
}

void stub_vfs_list_directory(void* instance, void* handle, FlString relative_path, FlVfsListingCallback callback,
                             void* user_data) {
    (void)instance;
    (void)relative_path;
    StubVfsHandle* h = (StubVfsHandle*)handle;
    if (!h || !h->is_dir || !callback) {
        return;
    }
    FlVfsEntry entry = {
        .name = FL_S(STUB_VFS_FILE),
        .size = (int64_t)sizeof(k_stub_vfs_data),
    };
    (void)callback(user_data, &entry);
}

void stub_vfs_close(void* instance, void* handle) {
    (void)instance;
    if (handle) {
        s_stub_vfs_close_count++;
    }
    free(handle);
}

FlVfsDriverCapabilities stub_vfs_get_capabilities(void) {
    return (FlVfsDriverCapabilities)(FlVfsDriverCapabilities_Read | FlVfsDriverCapabilities_List);
}

FlVfsPlugin s_stub_vfs_plugin = {
    .api_version = FL_VFS_PLUGIN_API_VERSION,
    .plugin_name = FL_S_("StubFS"),
    .plugin_info = NULL,
    .create = NULL,
    .destroy = NULL,
    .supports_path = stub_vfs_supports_path,
    .can_open = stub_vfs_can_open,
    .open = stub_vfs_open,
    .is_directory = stub_vfs_is_directory,
    .read = stub_vfs_read,
    .get_size = stub_vfs_get_size,
    .list_directory = stub_vfs_list_directory,
    .close = stub_vfs_close,
    .get_capabilities = stub_vfs_get_capabilities,
    .open_memory = NULL,
    .get_file_path = NULL,
    .is_cached = NULL,
};

RenderFixture s_render;

void render_fixture_capture(void* user_data, FlSurface* surface, const FlRenderFrame* frame) {
    s_render.submitted_global_alpha = frame->global_alpha;
    s_render.submitted_background = frame->background;
    s_render.border_found = false;
    s_render.text_run_found = false;
    for (const u8* p = (const u8*)frame->commands; p < (const u8*)frame->commands + frame->bytes;) {
        const FlCmdHeader* h = (const FlCmdHeader*)p;
        if (h->kind == FlCmd_Border && !s_render.border_found) {
            s_render.submitted_border = *(const FlCmdBorder*)(h + 1);
            s_render.border_found = true;
        } else if (h->kind == FlCmd_TextRun && !s_render.text_run_found) {
            const FlCmdTextRun* run = (const FlCmdTextRun*)(h + 1);
            s_render.submitted_text_run = *run;
            const FlGlyph* glyphs = (const FlGlyph*)(run + 1);
            u32 n = run->glyph_count;
            if (n > 4) {
                n = 4;
            }
            for (u32 g = 0; g < n; g++) {
                s_render.text_run_glyphs[g] = glyphs[g];
            }
            s_render.text_run_glyph_count = n;
            s_render.text_run_found = true;
        }
        p += h->byte_size;
    }
    s_render.render(user_data, surface, frame);
}

FlUiCtx* render_fixture_get(void) {
    if (s_render.created) {
        // Re-select the fixture's context (core singleton + Clay tree) on every fetch: a preceding case may have
        // created and torn down its own context, leaving a different context current.
        ui_make_current(s_render.ctx);
        s_render.ctx->focused_id = 0;
        s_render.ctx->last_focused_id = 0;
        return s_render.ctx;
    }

    // perf_fence scene_many_elements lays out more than Clay's 8192 default element cap. Must be raised before
    // ui_create - Clay_MinMemorySize/Clay_Initialize read it to size the layout arena.
    Clay_SetMaxElementCount(32768);

    s_render.arena = arena_new();
    s_render.backend = create_software_renderer(s_render.arena, nullptr, FontRenderMode_Grayscale);
    s_render.render = s_render.backend.render;
    s_render.backend.render = render_fixture_capture;
    UiShared* shared = ui_shared_create(s_render.arena, &s_render.backend);
    FlUiCtx* ctx = ui_create(s_render.arena, shared);
    if (!ctx) {
        return nullptr;
    }

    // Pair the context with its one render surface at the fixture's fixed 1920x1080 output size.
    ui_set_surface(ctx, s_render.backend.create_surface(s_render.backend.user_data, 1920, 1080));

    // ui_update_begin() pumps the VFS, so it must be initialized. No drivers are registered - headless
    // render-only frames never resolve a path.
    s_render.owns_vfs = fl_vfs_init(s_render.arena);

    // create_software_renderer (above) already created the SW pixel pool; this wires up the
    // load/decode system used by image_load_from_memory.
    s_render.owns_image_system = image_create_system(s_render.arena, &s_render.backend);

    // ui_update_begin() also pumps the font cache; it must be non-null even when a frame draws no text.
    s_render.font_cache = font_cache_new(s_render.arena);
    ui_set_font_cache(ctx, s_render.font_cache);

    s_render.ctx = ctx;
    s_render.created = true;
    return ctx;
}

// scene_many_elements: N flat fixed-size children in one clipped vertical container. Stresses layout O(n), id
// string-hashing and bin_tiles. N exceeds Clay's 8192 default element cap, so it only lays out because the
// fixture raised the cap via Clay_SetMaxElementCount.
void scene_many_elements(FlUiCtx* ctx, int n) {
    const FlColor fill = ui_color(80, 120, 200, 255);

    ui_update_begin(ctx);
    fl_layout_start(ID("scene_many_elements_root"));
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { .width = sizing_grow(), .height = sizing_grow() },
        .direction = FlLayoutDirection_TopToBottom,
        .color = ui_color(20, 20, 24, 255),
        .clip = { .vertical = true },
    });
    for (int i = 0; i < n; i++) {
        fl_layout_start(ID_INT("scene_many_elements_child", i));
        fl_layout_configure((FlLayoutConfig) { .sizing = { sizing_fixed(100), sizing_fixed(4) }, .color = fill });
        fl_layout_end();
    }
    fl_layout_end();
    ui_update_end(ctx, 1.0f / 60.0f);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.theme_scope - scoped subtree theme overrides (ui_theme_push / ui_theme_pop): an override replaces
// only its flagged slots and inherits the rest, nests, and unwinds on pop.

bool color_eq(FlColor a, FlColor b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// The render-command snapshot stores colors as f32 (0..255); our theme colors are u8. Compare within half an LSB.
bool slot_override_color_eq(UiColor4 got, FlColor want) {
    return fabsf(got.r - (float)want.r) < 0.5f && fabsf(got.g - (float)want.g) < 0.5f
           && fabsf(got.b - (float)want.b) < 0.5f && fabsf(got.a - (float)want.a) < 0.5f;
}

// The commands of one type that the last frame painted, in Clay's render order - parents before children, so a
// widget's own box precedes whatever it contains. A tab strip emits its pill (when it paints one) before the
// tabs and each tab before its label, so the k-th rectangle after the pill and the k-th text belong to the k-th
// tab.
int render_cmd_collect(UiRenderCommandType type, UiRenderCommand* out, int max) {
    int count = 0;
    const int n = ui_get_render_command_count();
    for (int i = 0; i < n && count < max; i++) {
        UiRenderCommand cmd;
        if (ui_get_render_command(i, &cmd) && cmd.type == type) {
            out[count++] = cmd;
        }
    }
    return count;
}

// The snapshot stores colors as f32 (0..255) against u8 theme colors; compare within half an LSB.
bool tab_color_eq(UiColor4 got, FlColor want) {
    return fabsf(got.r - (float)want.r) < 0.5f && fabsf(got.g - (float)want.g) < 0.5f
           && fabsf(got.b - (float)want.b) < 0.5f && fabsf(got.a - (float)want.a) < 0.5f;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// canvas - the Canvas seam: painter calls inside a layout element draw at that element's z, under its child
// elements, bracketed by the live layout scissor stack, with floating elements declared later on top. These
// cases drive a real frame through the software backend and sample the offscreen buffer (targeted pixel
// samples, robust to the SW pipeline's ~1 LSB rounding), plus assert the Clay→IR command
// structure via the debug render-command snapshot.

// Configure the shared render fixture for 1:1 logical→physical mapping over a known clear color (no background
// image), so element-local painter coordinates land at absolute pixels.
void canvas_fixture_reset(FlUiCtx* ctx) {
    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f); // identity scale: true 1:1 logical->physical, clearing any prior fractional scale
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });
    ui_background_set(IMAGE_HANDLE_INVALID); // clear any image/crossfade from earlier cases; resets alpha
    ui_background_set_clear_color(12, 12, 14);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// list_focus_request.* - ui_list_request_focus: the opt-in seam a freshly-opened popup calls on its open edge so
// keyboard nav and Enter/Space-commit work before any row click. It engages the list's has_focus AND claims the
// global focus ring together (so the focus sticks past the frame the trigger widget blurs), and seeds the focus
// cursor to a stable id (validated in begin, fallback to the first row) - only when the list did not already
// hold focus (the re-arm guard).

// True if this frame drew the keyboard focus ring - a border render command bearing the theme's border.focus
// colour (the cursor row's border; item_begin draws it only on the frame_focus_cursor row).
bool focus_ring_drawn(void) {
    FlColor want = fl_theme_colors()->border.focus;
    const int n = ui_get_render_command_count();
    for (int i = 0; i < n; i++) {
        UiRenderCommand cmd;
        if (!ui_get_render_command(i, &cmd) || cmd.type != UiRenderCmd_Border) {
            continue;
        }
        if (want.a > 0 && cmd.border_color.a > 0.0f && fabsf(cmd.border_color.r - want.r) < 1.0f
            && fabsf(cmd.border_color.g - want.g) < 1.0f && fabsf(cmd.border_color.b - want.b) < 1.0f) {
            return true;
        }
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A perf_counters session owns one descriptor, so the cases that open one compare this before and
// after to prove it was closed (core/os/perf_counters_linux.inc).

#if defined(__linux__)
#include <dirent.h>

// Count the process's open file descriptors via /proc/self/fd. The directory handle itself adds one
// entry, identical in both snapshots, so before/after comparisons stay exact.
int count_open_fds(void) {
    DIR* dir = opendir("/proc/self/fd");
    if (!dir) {
        return -1;
    }
    int count = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] != '.') {
            count++;
        }
    }
    closedir(dir);
    return count;
}
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Tear the render fixture down. Idempotent: a TU that never touched the fixture, or one that runs
// after an earlier teardown already ran, finds nothing to release.

void render_fixture_teardown(void) {
    if (!s_render.created) {
        return; // render cases were filtered out; nothing to tear down
    }
    UiShared* shared = s_render.ctx->shared;
    FlSurface* surface = s_render.ctx->surface;
    ui_destroy(s_render.ctx);
    // The shared resource tier outlives the per-window context; tear it down after ui_destroy,
    // mirroring app.c's fl_app_destroy ordering.
    ui_shared_destroy(shared);
    // Surfaces are host-owned and torn down before the backend that created them; frees the SW
    // per-surface arena.
    s_render.backend.destroy_surface(s_render.backend.user_data, surface);
    // The host owns the backend's lifetime: tear it down explicitly (frees the SW arenas).
    s_render.backend.destroy(s_render.backend.user_data);
    font_cache_destroy(s_render.font_cache);
    if (s_render.owns_image_system) {
        image_destroy_system();
    }
    if (s_render.owns_vfs && g_vfs_state != nullptr) {
        vfs_destroy();
    }
    fl_arena_destroy(s_render.arena);
    s_render.arena = nullptr;
    s_render.owns_image_system = false;
    s_render.owns_vfs = false;
    s_render.created = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void vfs_test_register_driver(const FlVfsPlugin* plugin) {
    if (g_vfs_state == nullptr) {
        return;
    }
    for (const VfsPluginEntry* entry = g_vfs_state->plugin_first; entry != nullptr; entry = entry->next) {
        if (entry->plugin == plugin) {
            return;
        }
    }
    vfs_register_driver(plugin, nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void process_singletons_reset(void) {
    app_identity_reset();
    fl_log_file_shutdown();
    atomic_store(&s_stub_vfs_read_release, true);
    render_fixture_teardown();
    image_destroy_system();
    if (g_vfs_state != nullptr) {
        vfs_destroy();
    }
    atomic_store(&s_stub_vfs_read_gate_armed, false);
    s_stub_vfs_override_data = nullptr;
    s_stub_vfs_override_size = 0;
    s_stub_vfs_open_as_file = false;
    s_stub_vfs_short_read = false;
    s_stub_vfs_oversize = false;
}
