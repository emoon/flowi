// Shared fixtures used by more than one unit_tests translation unit.

#include "unit_tests_common.h"

FlArena* s_arena;

#if defined(__linux__)
// Number of mappings in /proc/self/maps.
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
        lines++; // final line without a trailing newline
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

// Handles the driver handed out and handles that came back through close. The two match when nothing leaked.
_Atomic int s_stub_vfs_alloc_count;

_Atomic int s_stub_vfs_close_count;
_Atomic int s_stub_vfs_read_count; // reads that reached the driver

// When set, opening the file with no parent handle yields a file handle. By default an open with no parent is
// the mount-root open and yields a directory.
bool s_stub_vfs_open_as_file;

// When set, read reports one byte fewer than get_size promised.
bool s_stub_vfs_short_read;

bool s_stub_vfs_oversize;

// Encoded payload for the mounted-image tests. nullptr keeps the text payload in k_stub_vfs_data.
const u8* s_stub_vfs_override_data;

size_t s_stub_vfs_override_size;

const u8* stub_vfs_data(size_t* size) {
    if (s_stub_vfs_override_data) {
        *size = s_stub_vfs_override_size;
        return s_stub_vfs_override_data;
    }
    *size = sizeof(k_stub_vfs_data);
    return k_stub_vfs_data;
}

StubVfsHandle* stub_vfs_alloc(bool is_dir) {
    StubVfsHandle* h = (StubVfsHandle*)malloc(sizeof(StubVfsHandle));
    if (h) {
        h->is_dir = is_dir;
        h->offset = 0;
        s_stub_vfs_alloc_count++;
    }
    return h;
}

// Matches the single file relative to the mount root ("hello.txt") and absolute ("/hello.txt").
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

// Matches the mount sources the cases use ("/stub", "/stub-image-a", ...), with or without the leading slash.
// The bare root ("" or "/") matches too, because a case that drives the resolver by hand opens it directly.
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
    return true;
}

FlVfsOpenResult stub_vfs_open(void* instance, void* parent_handle, FlString path, uint32_t flags,
                              FlVfsMountOptions mount_options) {
    (void)instance;
    (void)flags;
    (void)mount_options;

    // A nullptr parent_handle is the mount-root open and returns the root directory, or a file handle for the
    // file when s_stub_vfs_open_as_file is set.
    //
    // The driver claims only the /stub* sources the cases mount. Driver resolution tries registered drivers before
    // the built-in LocalFS fallback, so a broader claim would shadow LocalFS for every path.
    if (parent_handle == nullptr) {
        if (!stub_vfs_is_stub_root(path) && !stub_vfs_is_file(path)) {
            return (FlVfsOpenResult) { .handle = nullptr, .error = stub_vfs_not_found() };
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
    return (FlVfsOpenResult) { .handle = nullptr, .error = stub_vfs_not_found() };
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
    // An empty path asks for the size of the open file.
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
    // A non-empty path is a size or existence probe under this handle. Only the stub file resolves.
    if (stub_vfs_is_file(relative_path)) {
        size_t data_size = 0;
        (void)stub_vfs_data(&data_size);
        return (FlVfsSizeResult) { .size = (int64_t)data_size, .error = stub_vfs_ok() };
    }
    return (FlVfsSizeResult) { .size = -1, .error = stub_vfs_not_found() };
}

// A gated read gives up after 20 seconds, so a case that fails while holding the gate cannot hang the run. The
// gate is described at its declaration in unit_tests_common.h.
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
    .plugin_info = nullptr,
    .create = nullptr,
    .destroy = nullptr,
    .supports_path = stub_vfs_supports_path,
    .can_open = stub_vfs_can_open,
    .open = stub_vfs_open,
    .is_directory = stub_vfs_is_directory,
    .read = stub_vfs_read,
    .get_size = stub_vfs_get_size,
    .list_directory = stub_vfs_list_directory,
    .close = stub_vfs_close,
    .get_capabilities = stub_vfs_get_capabilities,
    .open_memory = nullptr,
    .get_file_path = nullptr,
    .is_cached = nullptr,
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
        // An earlier case may have created and torn down its own context and left a different one current, so
        // every fetch selects the fixture's context again (core singleton and Clay tree).
        ui_make_current(s_render.ctx);
        s_render.ctx->focused_id = 0;
        s_render.ctx->last_focused_id = 0;
        return s_render.ctx;
    }

    // scene_many_elements lays out more than Clay's default cap of 8192 elements. The cap has to be raised
    // before ui_create, because Clay_MinMemorySize and Clay_Initialize read it to size the layout arena.
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

    // The fixture's output size is fixed at 1920x1080.
    ui_set_surface(ctx, s_render.backend.create_surface(s_render.backend.user_data, 1920, 1080));

    // ui_update_begin() pumps the VFS, so it must be initialized. No drivers are registered, because headless
    // render-only frames never resolve a path.
    s_render.owns_vfs = fl_vfs_init(s_render.arena);

    // create_software_renderer already created the SW pixel pool. This creates the load and decode system that
    // image_load_from_memory uses.
    s_render.owns_image_system = image_create_system(s_render.arena, &s_render.backend);

    // ui_update_begin() also pumps the font cache, which must be non-null even when a frame draws no text.
    s_render.font_cache = font_cache_new(s_render.arena);
    ui_set_font_cache(ctx, s_render.font_cache);

    s_render.ctx = ctx;
    s_render.created = true;
    return ctx;
}

// Lays out n flat fixed-size children in one clipped vertical container, which loads layout, id string hashing
// and bin_tiles. n may exceed Clay's default cap of 8192 elements because the fixture raises the cap with
// Clay_SetMaxElementCount.
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

bool color_eq(FlColor a, FlColor b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// The render-command snapshot stores colors as f32 in 0..255 and theme colors are u8, so compare within half an
// LSB.
bool slot_override_color_eq(UiColor4 got, FlColor want) {
    return fabsf(got.r - (float)want.r) < 0.5f && fabsf(got.g - (float)want.g) < 0.5f
           && fabsf(got.b - (float)want.b) < 0.5f && fabsf(got.a - (float)want.a) < 0.5f;
}

// Collects the commands of one type that the last frame painted, in Clay's render order: parents before
// children, so a widget's own box precedes whatever it contains. A tab strip emits its pill (when it paints one)
// before the tabs and each tab before its label, so the k-th rectangle after the pill and the k-th text belong
// to the k-th tab.
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

// Same half-LSB comparison as slot_override_color_eq.
bool tab_color_eq(UiColor4 got, FlColor want) {
    return fabsf(got.r - (float)want.r) < 0.5f && fabsf(got.g - (float)want.g) < 0.5f
           && fabsf(got.b - (float)want.b) < 0.5f && fabsf(got.a - (float)want.a) < 0.5f;
}

// Sets the shared render fixture to a 1:1 logical to physical mapping over a known clear color with no
// background image, so element-local painter coordinates land at absolute pixels.
void canvas_fixture_reset(FlUiCtx* ctx) {
    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f); // clears any fractional scale an earlier case left
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });
    ui_background_set(IMAGE_HANDLE_INVALID); // drops any image or crossfade from earlier cases and resets alpha
    ui_background_set_clear_color(12, 12, 14);
}

// True if this frame drew the keyboard focus ring: a border render command in the theme's border.focus color.
// item_begin draws it only on the frame_focus_cursor row.
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

#if defined(__linux__)
#include <dirent.h>

// Counts the process's open file descriptors through /proc/self/fd. The directory handle adds one entry to
// every count, so the difference between two counts is exact.
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

// Safe to call when the fixture was never created or is already torn down.
void render_fixture_teardown(void) {
    if (!s_render.created) {
        return;
    }
    UiShared* shared = s_render.ctx->shared;
    FlSurface* surface = s_render.ctx->surface;
    ui_destroy(s_render.ctx);
    // The shared resources outlive the per-window context, and a surface is destroyed before the backend that
    // created it.
    ui_shared_destroy(shared);
    s_render.backend.destroy_surface(s_render.backend.user_data, surface);
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

// Cases that provoke failures on purpose log thousands of expected errors, which bury a real failure in the
// output. Only fatal messages are shown unless FLOWI_TEST_LOGS is set. A case that reads its own messages back
// from the log file lowers the level itself, and the reset before the next case restores it.
static LogLevel test_log_level(void) {
    return getenv_os("FLOWI_TEST_LOGS") != nullptr ? LogLevel_Trace : LogLevel_Fatal;
}

void process_singletons_reset(void) {
    fl_log_set_level(test_log_level());
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
