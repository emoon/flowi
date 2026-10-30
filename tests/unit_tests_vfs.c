// VFS mount, driver and plugin tests

#include "vfs_test_support.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define VFS_AWAIT_FLAG_TIMEOUT_NS 20000000000ull

// Spins until flag is set or the deadline passes; false on the deadline. Every wait on a flag another thread sets goes
// through it, so a failed case reports instead of leaving the only worker spinning.
static bool vfs_await_flag(_Atomic bool* flag) {
    u64 deadline_ns = get_monotonic_time_ns_os() + VFS_AWAIT_FLAG_TIMEOUT_NS;
    while (!atomic_load(flag)) {
        if (get_monotonic_time_ns_os() >= deadline_ns) {
            return false;
        }
        sleep_ms(1);
    }
    return true;
}

// vfs_destroy must close each leaked plugin file handle exactly once and release operation-owned heap
// allocations and listing arenas.
//
// This deliberately does NOT diff mimalloc's byte counters across the cycle. mi_stats_get() copies
// _mi_stats_main, which only ever accumulates the stats of threads that have called mi_stats_merge()
// (or exited): mimalloc credits an allocation to the allocating thread and the matching free to the
// freeing thread. The VFS runs operations on job workers - fl_vfs_mount_read_all_with_options allocates
// its buffer on a worker, vfs_destroy frees it on this thread - so the worker's `+` sits in
// thread-local stats that are never merged while the worker lives, and only the `-` lands in the
// numbers we can read. The counter therefore drops ~216 bytes per cycle indefinitely, which says
// nothing about whether anything leaked. Heap release is covered instead by the close accounting
// below and by the /proc/self/maps check; a byte-accurate assertion needs a process-wide counter
// that flowi does not have yet.

// A leak adds at least one mapping per cycle, so the count may drift by less than the cycle count for reasons
// unrelated to the VFS (other threads mapping memory) without failing the case.
#define VFS_MAPS_CYCLES 24
#define VFS_MAPS_SLACK 8

UTEST(vfs, destroy_closes_leaked_handles_across_cycles) {
#if defined(__linux__)
    u64 maps_after_first_cycle = 0;
#endif
    for (int cycle = 0; cycle < VFS_MAPS_CYCLES; cycle++) {
        ASSERT_TRUE_MSG(fl_vfs_init(s_arena), "VFS singleton must be uninitialized when this test starts");
        vfs_test_register_driver(&s_stub_vfs_plugin);

        int allocs_before = s_stub_vfs_alloc_count;
        int closes_before = s_stub_vfs_close_count;
        FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
        ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
        vfs_wait_all();
        ASSERT_TRUE(fl_vfs_mount_is_ready(mount.mount));

        // Every resource-owning operation variant is left open on purpose: vfs_destroy must reap them all.
        FlVfsHandle file = fl_vfs_mount_open(mount.mount, FL_S(STUB_VFS_FILE), FlVfsOpenFlags_Read);
        ASSERT_NE(file, FL_VFS_HANDLE_INVALID);
        uint8_t read_buffer[sizeof(k_stub_vfs_data)] = { 0 };
        FlVfsHandle read = fl_vfs_read(file, read_buffer, sizeof(read_buffer));
        ASSERT_NE(read, FL_VFS_HANDLE_INVALID);
        FlVfsHandle read_all = fl_vfs_mount_read_all_with_options(mount.mount, FL_S(STUB_VFS_FILE), NULL, NULL, NULL,
                                                                  FL_VFS_HANDLE_INVALID);
        ASSERT_NE(read_all, FL_VFS_HANDLE_INVALID);
        FlVfsDirHandle list = fl_vfs_get_listing(mount.mount, FL_S(""), 0);
        ASSERT_NE(list, FL_VFS_DIR_HANDLE_INVALID);
        vfs_wait_all();

        ASSERT_TRUE(fl_vfs_get_data(read).success);
        ASSERT_TRUE(fl_vfs_get_data(read_all).success);
        ASSERT_TRUE(fl_vfs_get_file_list(list).success);

        vfs_destroy();

        int allocated = s_stub_vfs_alloc_count - allocs_before;
        int closed = s_stub_vfs_close_count - closes_before;
        ASSERT_GT_MSG(allocated, 0, "cycle should have opened plugin handles");
        ASSERT_EQ_MSG(closed, allocated, "vfs_destroy must close every leaked plugin handle exactly once");
#if defined(__linux__)
        u64 maps_after_cycle = count_proc_self_maps();
        if (cycle == 0) {
            maps_after_first_cycle = maps_after_cycle;
        }
        ASSERT_LE_MSG(maps_after_cycle, maps_after_first_cycle + VFS_MAPS_SLACK,
                      "repeated VFS cycles must not accumulate arena reservations");
#endif
    }
}

UTEST(vfs, builtin_localfs_reads_real_file) {
    FlArena* arena = arena_new();

    FlString cwd = get_current_directory_os(arena);
    FlString dir_path = file_build_path_os(arena, cwd, FL_S("unit_test_localfs_builtin_tmp"));
    FlString file_path = file_build_path_os(arena, dir_path, FL_S("hello.bin"));

    file_remove_directory_recursive_os(dir_path); // clean any leftover from an aborted run
    ASSERT_TRUE(file_create_directory_os(dir_path));

    // A fixed pattern, so a truncated or misordered read is caught by the byte compare below.
    uint8_t payload[8192];
    for (size_t i = 0; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)(i * 31u + 7u);
    }
    ASSERT_EQ((int)sizeof(payload), (int)file_write_os(file_path, payload, (i64)sizeof(payload)));

    fl_vfs_init(s_arena); // registers the built-in LocalFS driver

    FlVfsMountResult mount = vfs_mount(dir_path);
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "LocalFS should mount a real directory");
    ASSERT_TRUE(mount.mount != NULL);
    vfs_wait_all();
    ASSERT_TRUE(fl_vfs_mount_is_ready(mount.mount));

    FlVfsHandle read
        = fl_vfs_mount_read_all_with_options(mount.mount, FL_S("hello.bin"), NULL, NULL, NULL, FL_VFS_HANDLE_INVALID);
    ASSERT_NE(read, FL_VFS_HANDLE_INVALID);
    fl_vfs_wait(read);
    FlVfsData data = fl_vfs_get_data(read);
    ASSERT_TRUE_MSG(data.success, "read_all through built-in LocalFS should succeed");
    ASSERT_EQ((int)sizeof(payload), (int)data.size);
    ASSERT_TRUE(data.data != NULL);
    ASSERT_EQ(memcmp(data.data, payload, sizeof(payload)), 0);

    FlVfsDirHandle list = fl_vfs_get_listing(mount.mount, FL_S(""), 0);
    ASSERT_NE(list, FL_VFS_DIR_HANDLE_INVALID);
    fl_vfs_wait(list);
    FlVfsFileList files = fl_vfs_get_file_list(list);
    ASSERT_TRUE_MSG(files.success, "listing a real directory through built-in LocalFS should succeed");
    bool found = false;
    for (uint32_t i = 0; i < files.count; i++) {
        if (fl_string_equals(files.entries[i].name, FL_S("hello.bin"))) {
            found = true;
            ASSERT_FALSE(files.entries[i].is_directory);
            ASSERT_EQ((int)sizeof(payload), (int)files.entries[i].size);
        }
    }
    ASSERT_TRUE_MSG(found, "listing must include the file written to the mounted directory");

    // DirectPath: the driver hands back the real OS path, so VFS core can open on-disk archives by path
    // instead of loading them into RAM.
    const FlVfsPlugin* driver = localfs_plugin();
    ASSERT_TRUE((driver->get_capabilities() & FlVfsDriverCapabilities_DirectPath) != 0);
    ASSERT_TRUE(driver->get_file_path != NULL);
    FlVfsOpenResult opened = driver->open(NULL, NULL, file_path, FlVfsOpenFlags_Read, (FlVfsMountOptions) { 0 });
    ASSERT_EQ(opened.error.vfs_status, FlVfsStatus_Success);
    ASSERT_TRUE(opened.handle != NULL);
    FlString direct = driver->get_file_path(NULL, opened.handle, FL_S(""), arena);
    ASSERT_TRUE_MSG(fl_string_equals(direct, file_path), "get_file_path must return the file's real OS path");
    driver->close(NULL, opened.handle);

    fl_vfs_close(list);
    fl_vfs_close(read);
    vfs_destroy();

    file_remove_directory_recursive_os(dir_path);
    fl_arena_destroy(arena);
}

// get_size and read must agree on relative_path: what the first sizes, the second must be the bytes of.
UTEST(vfs, builtin_localfs_read_resolves_relative_path) {
    FlArena* arena = arena_new();

    FlString cwd = get_current_directory_os(arena);
    FlString dir_path = file_build_path_os(arena, cwd, FL_S("unit_test_localfs_relpath_tmp"));
    FlString file_path = file_build_path_os(arena, dir_path, FL_S("child.bin"));

    file_remove_directory_recursive_os(dir_path);
    ASSERT_TRUE(file_create_directory_os(dir_path));

    uint8_t payload[64];
    for (size_t i = 0; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)(i * 13u + 3u);
    }
    ASSERT_EQ((int)sizeof(payload), (int)file_write_os(file_path, payload, (i64)sizeof(payload)));

    const FlVfsPlugin* driver = localfs_plugin();

    FlVfsOpenResult dir = driver->open(NULL, NULL, dir_path, 0, (FlVfsMountOptions) { 0 });
    ASSERT_EQ(dir.error.vfs_status, FlVfsStatus_Success);
    ASSERT_TRUE(dir.is_directory);

    FlVfsSizeResult sized = driver->get_size(NULL, dir.handle, FL_S("child.bin"));
    ASSERT_EQ((int)sizeof(payload), (int)sized.size);

    uint8_t got[sizeof(payload)];
    memset(got, 0, sizeof(got));
    FlVfsReadResult read = driver->read(NULL, dir.handle, FL_S("child.bin"), got, sized.size);
    ASSERT_EQ(read.error.vfs_status, FlVfsStatus_Success);
    ASSERT_EQ((int)sizeof(payload), (int)read.bytes_read);
    ASSERT_EQ(memcmp(got, payload, sizeof(payload)), 0);

    // Reading the directory itself still fails rather than returning container bytes.
    FlVfsReadResult dir_read = driver->read(NULL, dir.handle, FL_S(""), got, 1);
    ASSERT_EQ((int)-1, (int)dir_read.bytes_read);

    // A file handle plus a child path resolves the path; it must not silently fall back to the handle's bytes.
    FlVfsOpenResult file = driver->open(NULL, NULL, file_path, FlVfsOpenFlags_Read, (FlVfsMountOptions) { 0 });
    ASSERT_EQ(file.error.vfs_status, FlVfsStatus_Success);
    FlVfsReadResult missing = driver->read(NULL, file.handle, FL_S("no_such.bin"), got, 1);
    ASSERT_EQ((int)-1, (int)missing.bytes_read);
    driver->close(NULL, file.handle);

    driver->close(NULL, dir.handle);
    file_remove_directory_recursive_os(dir_path);
    fl_arena_destroy(arena);
}

UTEST(vfs, read_through_generated_plugin) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    ASSERT_TRUE(mount.mount != NULL);
    vfs_wait_all(); // drain the async mount before reading
    ASSERT_TRUE(fl_vfs_mount_is_ready(mount.mount));

    FlVfsHandle handle
        = fl_vfs_mount_read_all_with_options(mount.mount, FL_S(STUB_VFS_FILE), NULL, NULL, NULL, FL_VFS_HANDLE_INVALID);
    ASSERT_NE(handle, FL_VFS_HANDLE_INVALID);
    fl_vfs_wait(handle);

    FlVfsData data = fl_vfs_get_data(handle);
    ASSERT_TRUE_MSG(data.success, "read through generated FlVfsPlugin should succeed");
    ASSERT_EQ((size_t)data.size, sizeof(k_stub_vfs_data));
    ASSERT_TRUE(data.data != NULL);
    ASSERT_EQ(memcmp(data.data, k_stub_vfs_data, sizeof(k_stub_vfs_data)), 0);

    StubVfsHandle root = { .is_dir = true, .offset = 0 };
    FlVfsOpenResult ok
        = s_stub_vfs_plugin.open(NULL, &root, FL_S(STUB_VFS_FILE), FlVfsOpenFlags_Read, (FlVfsMountOptions) { 0 });
    ASSERT_EQ(ok.error.vfs_status, FlVfsStatus_Success);
    ASSERT_TRUE(ok.handle != NULL);
    s_stub_vfs_plugin.close(NULL, ok.handle);

    FlVfsOpenResult miss
        = s_stub_vfs_plugin.open(NULL, &root, FL_S("nope.txt"), FlVfsOpenFlags_Read, (FlVfsMountOptions) { 0 });
    ASSERT_EQ(miss.error.vfs_status, FlVfsStatus_FileNotFound);
    ASSERT_TRUE(miss.handle == NULL);

    fl_vfs_mount_close(mount.mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlVfsDriverCapabilities memory_only_capabilities(void) {
    return FlVfsDriverCapabilities_Memory;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlVfsOpenResult memory_only_open_memory(void* instance, const uint8_t* data, uint64_t size, FlString path) {
    (void)instance;
    (void)data;
    (void)size;
    (void)path;
    return (FlVfsOpenResult) { 0 };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A driver that accepts archives from memory and nothing else.
static FlVfsPlugin s_memory_only_plugin = {
    .api_version = FL_VFS_PLUGIN_API_VERSION,
    .plugin_name = FL_S_("MemoryOnly"),
    .get_capabilities = memory_only_capabilities,
    .open_memory = memory_only_open_memory,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A short read (fewer bytes than get_size promised) takes the resolver's error branch, which must close
// the file handle try_open_file just opened before returning.
UTEST(vfs, resolve_short_read_closes_plugin_handle) {
    const int allocs_before = s_stub_vfs_alloc_count;
    const int closes_before = s_stub_vfs_close_count;

    s_stub_vfs_open_as_file = true;
    s_stub_vfs_short_read = true;

    VfsPluginEntry memory_entry = { .plugin = &s_memory_only_plugin };
    VfsState state = { .plugin_first = &memory_entry, .plugin_last = &memory_entry };
    VfsPluginEntry root_entry = { .plugin = &s_stub_vfs_plugin, .plugin_instance = NULL };

    // Root directory handle the resolver owns while walking the path (mirrors a mounted root).
    FlVfsOpenResult root = stub_vfs_open(NULL, NULL, FL_S("/"), 0, (FlVfsMountOptions) { 0 });
    ASSERT_TRUE(root.handle != NULL);
    ASSERT_TRUE(root.is_directory);

    PathComponents comp = path_components(s_arena, FL_S(STUB_VFS_FILE));
    FinalDriver result = vfs_resolve_from_root(&state, &root_entry, root.handle, &comp, 0, s_arena);

    s_stub_vfs_open_as_file = false;
    s_stub_vfs_short_read = false;

    ASSERT_EQ_MSG(result.error, VfsResolveError_InvalidFileSize, "short read must surface as InvalidFileSize");

    const int allocated = s_stub_vfs_alloc_count - allocs_before;
    const int closed = s_stub_vfs_close_count - closes_before;
    ASSERT_GT_MSG(allocated, 1, "resolver should have opened the root and the probed file");
    ASSERT_EQ_MSG(closed, allocated, "short-read error path must close every plugin handle it opened");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Descending through nested archives (a zip inside a zip) transitions to a new memory-mounted container at
// each layer, and each transition must close the outgoing container handle. This stub doubles as a file
// opener (can_open/open) and a memory-archive opener (open_memory): a probed "file" reads a few bytes, and
// open_memory mounts those bytes as a directory-shaped archive.

typedef struct NestHandle {
    bool is_dir;
    int serial; // 1-based allocation order; identifies a handle across a free that recycles its address
} NestHandle;

static int s_nest_alloc_count;

static int s_nest_close_count;

#define NEST_ARCHIVE_SIZE 8

static NestHandle* nest_alloc(bool is_dir) {
    NestHandle* h = (NestHandle*)malloc(sizeof(NestHandle));
    if (h) {
        h->is_dir = is_dir;
        h->serial = ++s_nest_alloc_count;
    }
    return h;
}

// Match a reconstructed path segment against a name, tolerating an optional leading '/'.
static bool nest_name_eq(FlString path, const char* name) {
    const char* d = path.data;
    int n = (int)path.length;
    if (n > 0 && d[0] == '/') {
        d++;
        n--;
    }
    int m = (int)strlen(name);
    return n == m && memcmp(d, name, (size_t)m) == 0;
}

static bool nest_can_open(FlString path) {
    return nest_name_eq(path, "l0") || nest_name_eq(path, "l1");
}

static FlVfsOpenResult nest_open(void* instance, void* parent_handle, FlString path, uint32_t flags,
                                 FlVfsMountOptions mount_options) {
    (void)instance;
    (void)parent_handle;
    (void)flags;
    (void)mount_options;
    if (nest_name_eq(path, "l0") || nest_name_eq(path, "l1")) {
        NestHandle* h = nest_alloc(false); // a file that open_memory will mount as the next archive layer
        return (
            FlVfsOpenResult) { .handle = h, .is_directory = false, .error = h ? stub_vfs_ok() : stub_vfs_not_found() };
    }
    return (FlVfsOpenResult) { .handle = NULL, .error = stub_vfs_not_found() };
}

static bool nest_is_directory(void* instance, void* handle) {
    (void)instance;
    NestHandle* h = (NestHandle*)handle;
    return h && h->is_dir;
}

static FlVfsSizeResult nest_get_size(void* instance, void* handle, FlString relative_path) {
    (void)instance;
    (void)handle;
    (void)relative_path;
    return (FlVfsSizeResult) { .size = (int64_t)NEST_ARCHIVE_SIZE, .error = stub_vfs_ok() };
}

static FlVfsReadResult nest_read(void* instance, void* handle, FlString relative_path, uint8_t* buffer, int64_t size) {
    (void)instance;
    (void)handle;
    (void)relative_path;
    if (buffer && size > 0) {
        memset(buffer, 0xAB, (size_t)size);
    }
    return (FlVfsReadResult) { .bytes_read = size, .error = stub_vfs_ok() };
}

static FlVfsOpenResult nest_open_memory(void* instance, const uint8_t* data, uint64_t size, FlString original_path) {
    (void)instance;
    (void)size;
    (void)original_path;
    // Takes ownership of the VFS-allocated buffer and frees it, mirroring a real archive plugin.
    mi_free((void*)data);
    NestHandle* h = nest_alloc(true);
    return (FlVfsOpenResult) { .handle = h, .is_directory = true, .error = h ? stub_vfs_ok() : stub_vfs_not_found() };
}

static FlVfsDriverCapabilities nest_get_capabilities(void) {
    return (FlVfsDriverCapabilities)(FlVfsDriverCapabilities_Read | FlVfsDriverCapabilities_List
                                     | FlVfsDriverCapabilities_Memory);
}

static void nest_close(void* instance, void* handle) {
    (void)instance;
    if (handle) {
        s_nest_close_count++;
    }
    free(handle);
}

static FlVfsPlugin s_nest_vfs_plugin = {
    .api_version = FL_VFS_PLUGIN_API_VERSION,
    .plugin_name = FL_S_("NestArc"),
    .can_open = nest_can_open,
    .open = nest_open,
    .is_directory = nest_is_directory,
    .read = nest_read,
    .get_size = nest_get_size,
    .close = nest_close,
    .get_capabilities = nest_get_capabilities,
    .open_memory = nest_open_memory,
};

UTEST(vfs, resolve_nested_archives_close_intermediate_handles) {
    const int allocs_before = s_nest_alloc_count;
    const int closes_before = s_nest_close_count;

    // State carrying the nested-archive stub so find_plugin_for_memory can mount each layer as we descend.
    VfsPluginEntry entry = { .plugin = &s_nest_vfs_plugin, .plugin_instance = NULL };
    VfsState state = { 0 };
    state.plugin_first = &entry;
    state.plugin_last = &entry;

    VfsPluginEntry root_entry = { .plugin = &s_nest_vfs_plugin, .plugin_instance = NULL };
    NestHandle* root = nest_alloc(true); // root directory the resolver walks from, and takes ownership of
    ASSERT_TRUE(root != NULL);

    // "l0/l1" descends through two archive layers, so the resolver must close the intermediate archive1
    // handle when it moves to archive2.
    const int root_serial = root->serial;
    PathComponents comp = path_components(s_arena, FL_S("l0/l1"));
    FinalDriver result = vfs_resolve_from_root(&state, &root_entry, root, &comp, 0, s_arena);

    ASSERT_EQ_MSG(result.error, VfsResolveError_None, "nested-archive path must resolve");
    ASSERT_TRUE(result.handle != NULL);
    // By serial, not by address: the resolver closes the root, and the allocator is free to hand its address
    // straight back for the innermost archive's handle.
    ASSERT_NE_MSG(((NestHandle*)result.handle)->serial, root_serial,
                  "resolved handle is the innermost archive, not the root");

    // The resolver consumed the root; only the final resolved handle is left for the caller to close.
    nest_close(NULL, result.handle);

    const int allocated = s_nest_alloc_count - allocs_before;
    const int closed = s_nest_close_count - closes_before;
    ASSERT_EQ_MSG(allocated, 5, "root + two probed files + two mounted archive layers");
    ASSERT_EQ_MSG(closed, allocated,
                  "descending through nested archives must close every intermediate container handle");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The resolver's segment-walking loop only runs when the root plugin matched a strict prefix of the mount path.
// This stub's open() resolves the first segment ("root") and nothing longer, so driving it through
// vfs_resolve_mount_path always enters that loop, and every loop exit must leave zero handles live apart from the
// one returned in FinalDriver.handle.

typedef struct PfxHandle {
    bool is_dir;
} PfxHandle;

static int s_pfx_alloc_count;

static int s_pfx_close_count;

static bool s_pfx_memory_mountable; // whether open_memory accepts a probed file's bytes as an archive

#define PFX_FILE_SIZE 8

static PfxHandle* pfx_alloc(bool is_dir) {
    PfxHandle* h = (PfxHandle*)malloc(sizeof(PfxHandle));
    if (h) {
        h->is_dir = is_dir;
        s_pfx_alloc_count++;
    }
    return h;
}

static bool pfx_can_open(FlString path) {
    return nest_name_eq(path, "dir") || nest_name_eq(path, "l0") || nest_name_eq(path, "l1")
           || nest_name_eq(path, "plain");
}

static FlVfsOpenResult pfx_open(void* instance, void* parent_handle, FlString path, uint32_t flags,
                                FlVfsMountOptions mount_options) {
    (void)instance;
    (void)parent_handle;
    (void)flags;
    (void)mount_options;

    const bool is_dir = nest_name_eq(path, "root") || nest_name_eq(path, "dir");
    const bool is_file = nest_name_eq(path, "l0") || nest_name_eq(path, "l1") || nest_name_eq(path, "plain");

    if (!is_dir && !is_file) {
        return (FlVfsOpenResult) { .handle = NULL, .error = stub_vfs_not_found() };
    }

    PfxHandle* h = pfx_alloc(is_dir);
    return (FlVfsOpenResult) { .handle = h, .is_directory = is_dir, .error = h ? stub_vfs_ok() : stub_vfs_not_found() };
}

static bool pfx_is_directory(void* instance, void* handle) {
    (void)instance;
    PfxHandle* h = (PfxHandle*)handle;
    return h && h->is_dir;
}

static FlVfsSizeResult pfx_get_size(void* instance, void* handle, FlString relative_path) {
    (void)instance;
    (void)handle;
    (void)relative_path;
    return (FlVfsSizeResult) { .size = (int64_t)PFX_FILE_SIZE, .error = stub_vfs_ok() };
}

static FlVfsReadResult pfx_read(void* instance, void* handle, FlString relative_path, uint8_t* buffer, int64_t size) {
    (void)instance;
    (void)handle;
    (void)relative_path;
    if (buffer && size > 0) {
        memset(buffer, 0xCD, (size_t)size);
    }
    return (FlVfsReadResult) { .bytes_read = size, .error = stub_vfs_ok() };
}

static FlVfsOpenResult pfx_open_memory(void* instance, const uint8_t* data, uint64_t size, FlString original_path) {
    (void)instance;
    (void)size;
    (void)original_path;

    // A rejecting plugin must leave the buffer alone; the resolver frees it.
    if (!s_pfx_memory_mountable) {
        return (FlVfsOpenResult) { .handle = NULL, .error = stub_vfs_not_found() };
    }

    mi_free((void*)data); // takes ownership, mirroring a real archive plugin
    PfxHandle* h = pfx_alloc(true);
    return (FlVfsOpenResult) { .handle = h, .is_directory = true, .error = h ? stub_vfs_ok() : stub_vfs_not_found() };
}

static FlVfsDriverCapabilities pfx_get_capabilities(void) {
    return (FlVfsDriverCapabilities)(FlVfsDriverCapabilities_Read | FlVfsDriverCapabilities_List
                                     | FlVfsDriverCapabilities_Memory);
}

static void pfx_close(void* instance, void* handle) {
    (void)instance;
    if (handle) {
        s_pfx_close_count++;
    }
    free(handle);
}

static FlVfsPlugin s_pfx_vfs_plugin = {
    .api_version = FL_VFS_PLUGIN_API_VERSION,
    .plugin_name = FL_S_("PrefixRoot"),
    .can_open = pfx_can_open,
    .open = pfx_open,
    .is_directory = pfx_is_directory,
    .read = pfx_read,
    .get_size = pfx_get_size,
    .close = pfx_close,
    .get_capabilities = pfx_get_capabilities,
    .open_memory = pfx_open_memory,
};

// Resolves path through the full mount entry point with only the prefix stub registered.
static FinalDriver pfx_mount(VfsState* state, VfsPluginEntry* entry, FlString path) {
    *entry = (VfsPluginEntry) { .plugin = &s_pfx_vfs_plugin, .plugin_instance = NULL };
    *state = (VfsState) { 0 };
    state->plugin_first = entry;
    state->plugin_last = entry;
    return vfs_resolve_mount_path(state, path, s_arena);
}

UTEST(vfs, mount_path_directory_exit_closes_container) {
    const int allocs_before = s_pfx_alloc_count;
    const int closes_before = s_pfx_close_count;
    s_pfx_memory_mountable = true;

    // "root" opens as the container, then "dir" consumes the rest of the path as a directory.
    VfsState state;
    VfsPluginEntry entry;
    FinalDriver result = pfx_mount(&state, &entry, FL_S("root/dir"));

    ASSERT_EQ_MSG(result.error, VfsResolveError_None, "directory at the end of the path must resolve");
    ASSERT_TRUE(result.handle != NULL);
    // The container was closed before we got here; the returned handle must still be usable.
    ASSERT_TRUE(pfx_is_directory(NULL, result.handle));

    pfx_close(NULL, result.handle);

    const int allocated = s_pfx_alloc_count - allocs_before;
    const int closed = s_pfx_close_count - closes_before;
    ASSERT_EQ_MSG(allocated, 2, "root container + resolved directory");
    ASSERT_EQ_MSG(closed, allocated, "the container handle must be closed once the directory is resolved");
}

UTEST(vfs, mount_path_not_mountable_exit_closes_container) {
    const int allocs_before = s_pfx_alloc_count;
    const int closes_before = s_pfx_close_count;
    s_pfx_memory_mountable = false;

    // "plain" reads back fine but no memory plugin accepts it, so the mount fails as NotMountable.
    VfsState state;
    VfsPluginEntry entry;
    FinalDriver result = pfx_mount(&state, &entry, FL_S("root/plain"));

    ASSERT_EQ_MSG(result.error, VfsResolveError_NotMountable, "a non-archive file must not be mountable");
    ASSERT_TRUE(result.handle == NULL);

    const int allocated = s_pfx_alloc_count - allocs_before;
    const int closed = s_pfx_close_count - closes_before;
    ASSERT_EQ_MSG(allocated, 2, "root container + probed file");
    ASSERT_EQ_MSG(closed, allocated, "an error exit must leave no plugin handle live");
}

UTEST(vfs, mount_path_missing_remainder_closes_container) {
    const int allocs_before = s_pfx_alloc_count;
    const int closes_before = s_pfx_close_count;
    s_pfx_memory_mountable = true;

    // Nothing under "root" matches "nope", so try_open_file reports FileNotFound without having opened
    // a handle - the one case where the resolver legitimately has no probed handle to close.
    VfsState state;
    VfsPluginEntry entry;
    FinalDriver result = pfx_mount(&state, &entry, FL_S("root/nope"));

    ASSERT_EQ_MSG(result.error, VfsResolveError_FileNotFound, "an unresolvable remainder must be FileNotFound");
    ASSERT_TRUE(result.handle == NULL);

    const int allocated = s_pfx_alloc_count - allocs_before;
    const int closed = s_pfx_close_count - closes_before;
    ASSERT_EQ_MSG(allocated, 1, "only the root container was ever opened");
    ASSERT_EQ_MSG(closed, allocated, "an error exit must leave no plugin handle live");
}

UTEST(vfs, mount_path_nested_archives_close_root_and_containers) {
    const int allocs_before = s_pfx_alloc_count;
    const int closes_before = s_pfx_close_count;
    s_pfx_memory_mountable = true;

    // "root/l0/l1" descends two archive layers below the root container, so the handles closed here are
    // the root plus an intermediate container.
    VfsState state;
    VfsPluginEntry entry;
    FinalDriver result = pfx_mount(&state, &entry, FL_S("root/l0/l1"));

    ASSERT_EQ_MSG(result.error, VfsResolveError_None, "nested-archive path must resolve");
    ASSERT_TRUE(result.handle != NULL);
    ASSERT_TRUE(pfx_is_directory(NULL, result.handle));

    pfx_close(NULL, result.handle);

    const int allocated = s_pfx_alloc_count - allocs_before;
    const int closed = s_pfx_close_count - closes_before;
    ASSERT_EQ_MSG(allocated, 5, "root + two probed files + two mounted archive layers");
    ASSERT_EQ_MSG(closed, allocated, "root and every intermediate container must be closed");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// fl_vfs_close must not deadlock when a read job is still pending. Reads are started and closed immediately,
// many times, so the worker is often mid-job when close runs. A regression hangs (CI timeout).

UTEST(vfs, close_during_pending_read_no_deadlock) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    vfs_wait_all(); // drain the async mount before reading

    for (int i = 0; i < 64; ++i) {
        FlVfsHandle handle = fl_vfs_mount_read_all_with_options(mount.mount, FL_S(STUB_VFS_FILE), NULL, NULL, NULL,
                                                                FL_VFS_HANDLE_INVALID);
        ASSERT_NE(handle, FL_VFS_HANDLE_INVALID);
        fl_vfs_close(handle); // close with the read job still pending - must not deadlock
    }

    // Sanity: a normal read still round-trips after all that churn.
    FlVfsHandle handle
        = fl_vfs_mount_read_all_with_options(mount.mount, FL_S(STUB_VFS_FILE), NULL, NULL, NULL, FL_VFS_HANDLE_INVALID);
    ASSERT_NE(handle, FL_VFS_HANDLE_INVALID);
    fl_vfs_wait(handle);
    FlVfsData data = fl_vfs_get_data(handle);
    ASSERT_TRUE_MSG(data.success, "read after close-churn should still succeed");
    ASSERT_EQ((size_t)data.size, sizeof(k_stub_vfs_data));
    fl_vfs_close(handle);

    fl_vfs_mount_close(mount.mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// fl_vfs_mount_close must drain in-flight jobs and reap leaked handles for THIS mount before it tears down the
// mount's root handle, strings and arena. Reads are fired and the mount closed WITHOUT waiting or closing the handles,
// many times: the close must drain them, reap the orphans, and leave the VFS reusable.

UTEST(vfs, mount_close_drains_inflight_and_reaps_handles) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    for (int i = 0; i < 32; ++i) {
        FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
        ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
        vfs_wait_all(); // make the mount ready so the reads below are the in-flight jobs

        // Neither waited nor closed: still in-flight and still leaked when the mount closes.
        for (int j = 0; j < 4; ++j) {
            FlVfsHandle h = fl_vfs_mount_read_all_with_options(mount.mount, FL_S(STUB_VFS_FILE), NULL, NULL, NULL,
                                                               FL_VFS_HANDLE_INVALID);
            ASSERT_NE(h, FL_VFS_HANDLE_INVALID);
        }

        fl_vfs_mount_close(mount.mount); // must drain the reads and reap their handles, not UAF or hang
    }

    // The VFS is still usable after all that: a fresh mount + read round-trips.
    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ(mount.status, FlVfsMountErrorStatus_Success);
    vfs_wait_all();
    FlVfsHandle handle
        = fl_vfs_mount_read_all_with_options(mount.mount, FL_S(STUB_VFS_FILE), NULL, NULL, NULL, FL_VFS_HANDLE_INVALID);
    ASSERT_NE(handle, FL_VFS_HANDLE_INVALID);
    fl_vfs_wait(handle);
    FlVfsData data = fl_vfs_get_data(handle);
    ASSERT_TRUE_MSG(data.success, "read after close-with-open-handles churn should still succeed");
    ASSERT_EQ((size_t)data.size, sizeof(k_stub_vfs_data));
    fl_vfs_close(handle);
    fl_vfs_mount_close(mount.mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle-lifetime contract: multi-threaded handle churn. Several threads run the full
// open/read/wait/get-result/close cycle on overlapping paths while a hostile thread steals handle ids out of
// shared slots and closes them (so close races wait/get_data/close on the same id), and the main thread
// closes a second mount mid-churn with reads still in flight (drain + reap under concurrent map traffic).
// Each second-mount close waits for the churn threads to complete another batch of cycles.

#define VFS_CHURN_WAIT_MS 5000
#define VFS_CHURN_BATCH 20
#define VFS_WAIT_ALL_CHURN_HANDLE_TARGET 2000
#define VFS_WAIT_ALL_CHURN_WAIT_TARGET 200

typedef struct VfsChurnCtx {
    FlVfsMount* mount;
    _Atomic bool stop;
    _Atomic(FlVfsHandle) slots[4]; // Churn threads publish live handles here; the closer thread steals them
    _Atomic int cycles;
} VfsChurnCtx;

static void* vfs_churn_thread(void* user_data) {
    VfsChurnCtx* ctx = user_data;
    // Overlapping paths: every thread reads the same file, so all threads hit the same driver handle
    unsigned iter = 0;
    while (!atomic_load(&ctx->stop)) {
        FlVfsHandle h = fl_vfs_mount_read_all_with_options(ctx->mount, FL_S(STUB_VFS_FILE), NULL, NULL, NULL,
                                                           FL_VFS_HANDLE_INVALID);
        if (h == FL_VFS_HANDLE_INVALID) {
            continue;
        }

        if (iter++ & 1) {
            // Shared handle: publish it so the closer thread races a close against the calls below. Only
            // lookup-safe APIs here - dereferencing the result payload concurrently with a close of the
            // same handle is caller error by contract, so no fl_vfs_get_data on this branch.
            atomic_store(&ctx->slots[(iter / 2) % 4], h);
            fl_vfs_wait(h);
            (void)fl_vfs_is_ready(h);
            (void)vfs_get_status(h);
            fl_vfs_close(h); // May double-close with the closer thread - second close must be a clean no-op
        } else {
            // Private handle: full open/read/wait/get-result/close cycle, payload deref included
            fl_vfs_wait(h);
            (void)fl_vfs_is_ready(h);
            (void)vfs_get_status(h);
            FlVfsData data = fl_vfs_get_data(h);
            (void)data;
            fl_vfs_close(h);
        }
        atomic_fetch_add(&ctx->cycles, 1);
    }
    return nullptr;
}

static void* vfs_closer_thread(void* user_data) {
    VfsChurnCtx* ctx = user_data;
    while (!atomic_load(&ctx->stop)) {
        for (int i = 0; i < 4; i++) {
            FlVfsHandle h = atomic_exchange(&ctx->slots[i], FL_VFS_HANDLE_INVALID);
            if (h != FL_VFS_HANDLE_INVALID) {
                fl_vfs_close(h); // Races the owner's wait/get_data/close on the same id
            }
        }
        // Also churn its own handle: close with the read job possibly still pending
        FlVfsHandle h = fl_vfs_mount_read_all_with_options(ctx->mount, FL_S(STUB_VFS_FILE), NULL, NULL, NULL,
                                                           FL_VFS_HANDLE_INVALID);
        if (h != FL_VFS_HANDLE_INVALID) {
            fl_vfs_close(h);
        }
    }
    return nullptr;
}

UTEST(vfs, handle_lifetime_multithreaded_churn) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    vfs_wait_all();
    ASSERT_TRUE(fl_vfs_mount_is_ready(mount.mount));

    VfsChurnCtx ctx = { .mount = mount.mount };
    for (int i = 0; i < 4; i++) {
        atomic_init(&ctx.slots[i], FL_VFS_HANDLE_INVALID);
    }

    Thread churn[3];
    Thread closer;
    for (int i = 0; i < 3; i++) {
        ASSERT_TRUE(os_thread_create(&churn[i], vfs_churn_thread, &ctx));
    }
    ASSERT_TRUE(os_thread_create(&closer, vfs_closer_thread, &ctx));

    // Mid-churn: close a second mount with reads still in flight, so the drain + reap sweep runs while the
    // other threads hammer the handle map. The churn threads never touch this mount.
    for (int i = 0; i < 8; i++) {
        FlVfsMountResult second = vfs_mount(FL_S("/stub"));
        ASSERT_EQ(second.status, FlVfsMountErrorStatus_Success);
        for (int waited = 0; !fl_vfs_mount_is_ready(second.mount) && waited < 2000; waited++) {
            sleep_ms(1);
        }
        ASSERT_TRUE(fl_vfs_mount_is_ready(second.mount));
        for (int j = 0; j < 4; j++) {
            (void)fl_vfs_mount_read_all_with_options(second.mount, FL_S(STUB_VFS_FILE), NULL, NULL, NULL,
                                                     FL_VFS_HANDLE_INVALID);
        }
        const int cycles_at_close = atomic_load(&ctx.cycles);
        for (int waited = 0; waited < VFS_CHURN_WAIT_MS && atomic_load(&ctx.cycles) < cycles_at_close + VFS_CHURN_BATCH;
             waited++) {
            sleep_ms(1);
        }
        fl_vfs_mount_close(second.mount);
    }

    atomic_store(&ctx.stop, true);
    for (int i = 0; i < 3; i++) {
        os_thread_join(&churn[i]);
    }
    os_thread_join(&closer);

    // Reap any handle still parked in a slot when the loops stopped
    for (int i = 0; i < 4; i++) {
        FlVfsHandle h = atomic_exchange(&ctx.slots[i], FL_VFS_HANDLE_INVALID);
        if (h != FL_VFS_HANDLE_INVALID) {
            fl_vfs_close(h);
        }
    }

    ASSERT_GT(atomic_load(&ctx.cycles), 0); // The churn actually ran

    // The VFS survived: a normal read still round-trips
    FlVfsHandle h
        = fl_vfs_mount_read_all_with_options(mount.mount, FL_S(STUB_VFS_FILE), NULL, NULL, NULL, FL_VFS_HANDLE_INVALID);
    ASSERT_NE(h, FL_VFS_HANDLE_INVALID);
    fl_vfs_wait(h);
    FlVfsData data = fl_vfs_get_data(h);
    ASSERT_TRUE_MSG(data.success, "read after multithreaded churn should still succeed");
    ASSERT_EQ((size_t)data.size, sizeof(k_stub_vfs_data));
    fl_vfs_close(h);

    fl_vfs_mount_close(mount.mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// vfs_wait_all collects the jobs of every live op while other threads create and close ops. Each churn thread owns
// its handles through close, isolating the collection race from the same-handle races covered above.

typedef struct VfsWaitAllChurnCtx {
    FlVfsMount* mount;
    _Atomic bool stop;
    _Atomic int handle_cycles;
    _Atomic int wait_cycles;
} VfsWaitAllChurnCtx;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* vfs_wait_all_handle_churn_thread(void* user_data) {
    VfsWaitAllChurnCtx* ctx = user_data;
    while (!atomic_load(&ctx->stop)) {
        FlVfsHandle handle = fl_vfs_mount_read_all_with_options(ctx->mount, FL_S(STUB_VFS_FILE), NULL, NULL, NULL,
                                                                FL_VFS_HANDLE_INVALID);
        if (handle != FL_VFS_HANDLE_INVALID) {
            fl_vfs_wait(handle);
            fl_vfs_close(handle);
            atomic_fetch_add(&ctx->handle_cycles, 1);
        }
    }
    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* vfs_wait_all_churn_thread(void* user_data) {
    VfsWaitAllChurnCtx* ctx = user_data;
    while (!atomic_load(&ctx->stop)) {
        vfs_wait_all();
        atomic_fetch_add(&ctx->wait_cycles, 1);
    }
    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(vfs, wait_all_concurrent_op_churn) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    vfs_wait_all();

    VfsWaitAllChurnCtx ctx = { .mount = mount.mount };
    Thread churn[3];
    Thread waiter;
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(os_thread_create(&churn[i], vfs_wait_all_handle_churn_thread, &ctx));
    }
    ASSERT_TRUE(os_thread_create(&waiter, vfs_wait_all_churn_thread, &ctx));

    for (int waited = 0; waited < VFS_CHURN_WAIT_MS
                         && (atomic_load(&ctx.handle_cycles) < VFS_WAIT_ALL_CHURN_HANDLE_TARGET
                             || atomic_load(&ctx.wait_cycles) < VFS_WAIT_ALL_CHURN_WAIT_TARGET);
         waited++) {
        sleep_ms(1);
    }
    atomic_store(&ctx.stop, true);
    for (int i = 0; i < 3; ++i) {
        os_thread_join(&churn[i]);
    }
    os_thread_join(&waiter);

    ASSERT_GE(atomic_load(&ctx.handle_cycles), VFS_WAIT_ALL_CHURN_HANDLE_TARGET);
    ASSERT_GE(atomic_load(&ctx.wait_cycles), VFS_WAIT_ALL_CHURN_WAIT_TARGET);
    fl_vfs_mount_close(mount.mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle-lifetime contract: reads chained onto a file keep the file alive past its own close, so the file's driver
// handle is never closed under a read that is queued or running.

UTEST(vfs, file_handle_chained_reads_survive_file_close) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    vfs_wait_all();
    ASSERT_TRUE(fl_vfs_mount_is_ready(mount.mount));

    FlVfsHandle file = fl_vfs_mount_open(mount.mount, FL_S(STUB_VFS_FILE), FlVfsOpenFlags_Read);
    ASSERT_NE(file, FL_VFS_HANDLE_INVALID);

    u8 buf1[sizeof(k_stub_vfs_data)] = { 0 };
    u8 buf2[sizeof(k_stub_vfs_data)] = { 0 };
    FlVfsHandle r1 = fl_vfs_read(file, buf1, sizeof(buf1)); // chains onto the (possibly unfinished) open job
    FlVfsHandle r2 = fl_vfs_read(file, buf2, sizeof(buf2)); // chains onto r1
    ASSERT_NE(r1, FL_VFS_HANDLE_INVALID);
    ASSERT_NE(r2, FL_VFS_HANDLE_INVALID);

    fl_vfs_wait(r2); // r2 depends on r1 depends on open, so this drains the whole chain
    FlVfsData d1 = fl_vfs_get_data(r1);
    ASSERT_TRUE_MSG(d1.success, "first chained read should succeed");
    ASSERT_EQ((size_t)d1.size, sizeof(k_stub_vfs_data));
    ASSERT_EQ(memcmp(buf1, k_stub_vfs_data, sizeof(k_stub_vfs_data)), 0);
    FlVfsData d2 = fl_vfs_get_data(r2);
    ASSERT_TRUE_MSG(d2.success, "second chained read should succeed (EOF, 0 bytes)");

    fl_vfs_close(r1);
    fl_vfs_close(r2);
    fl_vfs_close(file);

    // Close the file with chained reads still pending; iterate so the worker is often mid-chain.
    for (int i = 0; i < 16; ++i) {
        FlVfsHandle f = fl_vfs_mount_open(mount.mount, FL_S(STUB_VFS_FILE), FlVfsOpenFlags_Read);
        ASSERT_NE(f, FL_VFS_HANDLE_INVALID);
        u8 buf[sizeof(k_stub_vfs_data)] = { 0 };
        FlVfsHandle r = fl_vfs_read(f, buf, sizeof(buf));
        ASSERT_NE(r, FL_VFS_HANDLE_INVALID);
        fl_vfs_close(f); // returns at once; the file handle is kept until the chained read has run
        // The read handle survives its file's close; its result is still harvestable.
        fl_vfs_wait(r);
        ASSERT_TRUE(fl_vfs_is_ready(r));
        ASSERT_TRUE(fl_vfs_get_data(r).success);
        fl_vfs_close(r);
    }

    fl_vfs_mount_close(mount.mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A read-all of a file whose reported size exceeds the 512 MB cap must fail hard before allocating the
// buffer - never return truncated data as success - and still close the plugin file handle.

UTEST(vfs, read_all_over_cap_reports_error_and_closes_handle) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    vfs_wait_all();
    ASSERT_TRUE(fl_vfs_mount_is_ready(mount.mount));

    const int allocs_before = s_stub_vfs_alloc_count;
    const int closes_before = s_stub_vfs_close_count;

    s_stub_vfs_oversize = true;
    FlVfsHandle handle
        = fl_vfs_mount_read_all_with_options(mount.mount, FL_S(STUB_VFS_FILE), NULL, NULL, NULL, FL_VFS_HANDLE_INVALID);
    ASSERT_NE(handle, FL_VFS_HANDLE_INVALID);
    fl_vfs_wait(handle);
    s_stub_vfs_oversize = false;

    ASSERT_EQ_MSG(vfs_get_status(handle), (VfsOpStatus)VFS_ERROR_FILE_TOO_LARGE,
                  "over-cap read-all must report VFS_ERROR_FILE_TOO_LARGE");
    FlVfsData data = fl_vfs_get_data(handle);
    ASSERT_FALSE_MSG(data.success, "over-cap read-all must not report success");

    const int opened = s_stub_vfs_alloc_count - allocs_before;
    const int closed = s_stub_vfs_close_count - closes_before;
    ASSERT_GT_MSG(opened, 0, "read-all should have opened at least one plugin handle");
    ASSERT_EQ_MSG(closed, opened, "over-cap error path must close every plugin handle it opened");

    fl_vfs_close(handle);
    fl_vfs_mount_close(mount.mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A missing file, a failed size query and an over-cap file each end a read-all with their own error status.

static bool s_size_query_fails;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static FlVfsSizeResult maybe_failing_size(void* instance, void* handle, FlString relative_path) {
    if (s_size_query_fails) {
        return (FlVfsSizeResult) { .size = -1 };
    }
    return s_stub_vfs_plugin.get_size(instance, handle, relative_path);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static VfsOpStatus read_all_status(FlVfsMount* mount, FlString path) {
    FlVfsHandle handle = fl_vfs_mount_read_all_with_options(mount, path, NULL, NULL, NULL, FL_VFS_HANDLE_INVALID);
    if (handle == FL_VFS_HANDLE_INVALID) {
        return VFS_STATE_SUCCESS;
    }
    fl_vfs_wait(handle);
    VfsOpStatus status = vfs_get_status(handle);
    fl_vfs_close(handle);
    return status;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
UTEST(vfs, missing_file_read_failure_and_over_cap_file_report_distinct_statuses) {
    ASSERT_TRUE(vfs_test_begin());
    static FlVfsPlugin wrapped;
    wrapped = s_stub_vfs_plugin;
    wrapped.get_size = maybe_failing_size;
    vfs_test_register_driver(&wrapped);

    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ(mount.status, FlVfsMountErrorStatus_Success);
    vfs_wait_all();
    ASSERT_TRUE(fl_vfs_mount_is_ready(mount.mount));

    const VfsOpStatus missing = read_all_status(mount.mount, FL_S("no_such_file.txt"));

    s_size_query_fails = true;
    const VfsOpStatus read_failure = read_all_status(mount.mount, FL_S(STUB_VFS_FILE));
    s_size_query_fails = false;

    s_stub_vfs_oversize = true;
    const VfsOpStatus over_cap = read_all_status(mount.mount, FL_S(STUB_VFS_FILE));
    s_stub_vfs_oversize = false;

    ASSERT_EQ((VfsOpStatus)VFS_ERROR_FILE_NOT_FOUND, missing);
    ASSERT_EQ((VfsOpStatus)VFS_ERROR_READ, read_failure);
    ASSERT_EQ((VfsOpStatus)VFS_ERROR_FILE_TOO_LARGE, over_cap);
    ASSERT_NE(missing, read_failure);
    ASSERT_NE(missing, over_cap);
    ASSERT_NE(read_failure, over_cap);

    fl_vfs_mount_close(mount.mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The archive drivers are asked in the order fl_vfs_register_builtin_drivers documents: lhasa, unlzx, libarchive, then
// the LocalFS fallback.

UTEST(vfs, builtin_drivers_are_probed_lhasa_unlzx_libarchive_then_localfs) {
    ASSERT_TRUE(vfs_test_begin());

    const FlVfsPlugin* expected[4];
    int expected_count = 0;
#if FLOWI_VFS_DRIVER_LHASA
    expected[expected_count++] = vfs_driver_lhasa();
#endif
#if FLOWI_VFS_DRIVER_UNLZX
    expected[expected_count++] = vfs_driver_unlzx();
#endif
#if FLOWI_VFS_DRIVER_LIBARCHIVE
    expected[expected_count++] = vfs_driver_libarchive();
#endif

    VfsPluginEntry* entry = g_vfs_state->plugin_first;
    for (int i = 0; i < expected_count; i++) {
        ASSERT_NE(entry, nullptr);
        ASSERT_EQ(entry->plugin, expected[i]);
        entry = entry->next;
    }
    ASSERT_EQ(entry, g_vfs_state->builtin_fallback);
    ASSERT_EQ(entry->next, nullptr);

    vfs_destroy();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A file the resolver would have to read into memory to offer to a memory driver, larger than the cap, fails the
// resolve without a single read.

UTEST(vfs, resolve_over_cap_file_fails_without_reading_it) {
    s_stub_vfs_open_as_file = true;
    s_stub_vfs_oversize = true;
    const int reads_before = atomic_load(&s_stub_vfs_read_count);
    const int allocs_before = s_stub_vfs_alloc_count;
    const int closes_before = s_stub_vfs_close_count;

    VfsPluginEntry memory_entry = { .plugin = &s_memory_only_plugin };
    VfsState state = { .plugin_first = &memory_entry, .plugin_last = &memory_entry };
    VfsPluginEntry root_entry = { .plugin = &s_stub_vfs_plugin, .plugin_instance = NULL };

    FlVfsOpenResult root = stub_vfs_open(NULL, NULL, FL_S("/"), 0, (FlVfsMountOptions) { 0 });
    ASSERT_TRUE(root.handle != NULL);

    PathComponents comp = path_components(s_arena, FL_S(STUB_VFS_FILE));
    FinalDriver result = vfs_resolve_from_root(&state, &root_entry, root.handle, &comp, 0, s_arena);

    s_stub_vfs_open_as_file = false;
    s_stub_vfs_oversize = false;

    ASSERT_EQ(result.error, VfsResolveError_FileTooLarge);
    ASSERT_EQ(atomic_load(&s_stub_vfs_read_count), reads_before);
    ASSERT_EQ(s_stub_vfs_close_count - closes_before, s_stub_vfs_alloc_count - allocs_before);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// With no driver that opens archives from memory, a file is not mountable and is not read to find that out.

UTEST(vfs, resolve_without_a_memory_driver_does_not_read_the_file) {
    s_stub_vfs_open_as_file = true;
    const int reads_before = atomic_load(&s_stub_vfs_read_count);
    const int allocs_before = s_stub_vfs_alloc_count;
    const int closes_before = s_stub_vfs_close_count;

    VfsState state = { 0 };
    VfsPluginEntry root_entry = { .plugin = &s_stub_vfs_plugin, .plugin_instance = NULL };

    FlVfsOpenResult root = stub_vfs_open(NULL, NULL, FL_S("/"), 0, (FlVfsMountOptions) { 0 });
    ASSERT_TRUE(root.handle != NULL);

    PathComponents comp = path_components(s_arena, FL_S(STUB_VFS_FILE));
    FinalDriver result = vfs_resolve_from_root(&state, &root_entry, root.handle, &comp, 0, s_arena);

    s_stub_vfs_open_as_file = false;

    ASSERT_EQ(result.error, VfsResolveError_NotMountable);
    ASSERT_EQ(atomic_load(&s_stub_vfs_read_count), reads_before);
    ASSERT_EQ(s_stub_vfs_close_count - closes_before, s_stub_vfs_alloc_count - allocs_before);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// fl_vfs_init without an arena makes its own; vfs_destroy has to give it back.

UTEST(vfs, init_without_an_arena_frees_the_arena_it_makes) {
#if defined(__linux__)
    u64 maps_after_first_cycle = 0;
#endif
    for (int cycle = 0; cycle < VFS_MAPS_CYCLES; cycle++) {
        ASSERT_TRUE(fl_vfs_init(nullptr));
        vfs_destroy();
#if defined(__linux__)
        u64 maps_after_cycle = count_proc_self_maps();
        if (cycle == 0) {
            maps_after_first_cycle = maps_after_cycle;
        }
        ASSERT_LE_MSG(maps_after_cycle, maps_after_first_cycle + VFS_MAPS_SLACK,
                      "each cycle must give back its own arena");
#endif
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Many failures in a row do not exhaust error-string storage.

#define ERROR_LOOP_COUNT 6000
#define ERROR_LOOP_PATH_SIZE 240

UTEST(vfs, error_strings_do_not_run_out_over_many_failures) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ(mount.status, FlVfsMountErrorStatus_Success);
    vfs_wait_all();
    ASSERT_TRUE(fl_vfs_mount_is_ready(mount.mount));

    char name[ERROR_LOOP_PATH_SIZE + 1];
    memset(name, 'x', ERROR_LOOP_PATH_SIZE);
    name[ERROR_LOOP_PATH_SIZE] = '\0';
    FlString missing = string_from_cstr(name);

    u64 error_bytes = 0;
    for_count(i, ERROR_LOOP_COUNT) {
        FlVfsHandle handle = vfs_mount_read_all(mount.mount, missing);
        ASSERT_NE(handle, FL_VFS_HANDLE_INVALID);
        fl_vfs_wait(handle);
        FlVfsData data = fl_vfs_get_data(handle);
        ASSERT_FALSE(data.success);
        const u64 message_length = data.error_message.length;
        ASSERT_GT(message_length, (u64)ERROR_LOOP_PATH_SIZE);
        error_bytes += message_length;
        fl_vfs_close(handle);
    }
    ASSERT_GT(error_bytes, (u64)MB(1));

    fl_vfs_mount_close(mount.mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(vfs, failed_listings_carry_an_error_message_until_closed) {
    ASSERT_TRUE(vfs_test_begin());
    static FlVfsPlugin no_list;
    no_list = s_stub_vfs_plugin;
    no_list.list_directory = NULL;
    vfs_test_register_driver(&no_list);

    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ(mount.status, FlVfsMountErrorStatus_Success);
    vfs_wait_all();
    ASSERT_TRUE(fl_vfs_mount_is_ready(mount.mount));

    char name[ERROR_LOOP_PATH_SIZE + 1];
    memset(name, 'x', ERROR_LOOP_PATH_SIZE);
    name[ERROR_LOOP_PATH_SIZE] = '\0';
    FlString long_path = string_from_cstr(name);

    u64 error_bytes = 0;
    for_count(i, ERROR_LOOP_COUNT) {
        FlVfsDirHandle handle = fl_vfs_get_listing(mount.mount, long_path, 0);
        ASSERT_NE(handle, FL_VFS_DIR_HANDLE_INVALID);
        fl_vfs_wait(handle);
        FlVfsFileList list = fl_vfs_get_file_list(handle);
        const bool list_ok = list.success;
        const u64 message_length = list.error_message.length;
        ASSERT_FALSE(list_ok);
        ASSERT_GT(message_length, 0u);
        error_bytes += message_length;
        fl_vfs_close(handle);
    }
    ASSERT_GT(error_bytes, (u64)MB(1));

    fl_vfs_mount_close(mount.mount);
    vfs_destroy();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A read-all callback owns whatever its user_data points at, so it must be invoked on failure too - with no
// data - or that ownership is stranded. Covers both notification sites: the in-job error path
// (read_all_set_error, reached here via the over-cap branch) and the pre-dispatch cancellation gate.

typedef struct FailNotifyStats {
    _Atomic int calls;
    _Atomic int calls_with_data;
    _Atomic int releases;
} FailNotifyStats;

static void fail_notify_release(void* user_data) {
    atomic_fetch_add(&((FailNotifyStats*)user_data)->releases, 1);
}

static FlVfsData fail_notify_callback(void* data, i64 size, void* user_data) {
    FailNotifyStats* stats = (FailNotifyStats*)user_data;
    if (data || size != 0) {
        atomic_fetch_add(&stats->calls_with_data, 1);
    }
    atomic_fetch_add(&stats->calls, 1);
    return (FlVfsData) { .success = false };
}

UTEST(vfs, read_all_failure_notifies_the_callback) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    vfs_wait_all();
    ASSERT_TRUE(fl_vfs_mount_is_ready(mount.mount));

    static FailNotifyStats over_cap;
    over_cap = (FailNotifyStats) { 0 };
    s_stub_vfs_oversize = true;
    FlVfsHandle handle = fl_vfs_mount_read_all_with_options(mount.mount, FL_S(STUB_VFS_FILE), fail_notify_callback,
                                                            &over_cap, fail_notify_release, FL_VFS_HANDLE_INVALID);
    ASSERT_NE(handle, FL_VFS_HANDLE_INVALID);
    fl_vfs_wait(handle);
    s_stub_vfs_oversize = false;

    ASSERT_EQ_MSG(vfs_get_status(handle), (VfsOpStatus)VFS_ERROR_FILE_TOO_LARGE,
                  "over-cap read-all must report VFS_ERROR_FILE_TOO_LARGE");
    ASSERT_EQ_MSG(atomic_load(&over_cap.calls), 1, "an in-job read failure must notify the callback exactly once");
    ASSERT_EQ_MSG(atomic_load(&over_cap.calls_with_data), 0, "a failure notification carries no data");
    ASSERT_EQ(atomic_load(&over_cap.releases), 0);
    fl_vfs_close(handle);
    ASSERT_EQ_MSG(atomic_load(&over_cap.releases), 1, "a failed read's close releases user_data exactly once");

    fl_vfs_mount_close(mount.mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// fl_jobs_wait() does nothing on a job worker, so fl_vfs_wait() cannot wait out a running job from one. fl_vfs_close()
// needs no wait: it only gives up the id, and the running job keeps the op alive until it ends. The read-all callback
// is the reachable form of that: it runs on the worker inside the read's own job, so a close issued there is one
// against an in-flight handle.

typedef struct VfsWorkerCloseCtx {
    FlVfsHandle handle;
    _Atomic bool ready_during_wait;
    _Atomic bool close_attempted;
    _Atomic int releases;
    _Atomic bool released_after_close;
} VfsWorkerCloseCtx;

static FlVfsData vfs_worker_close_callback(void* data, i64 size, void* user_data) {
    VfsWorkerCloseCtx* ctx = (VfsWorkerCloseCtx*)user_data;

    // fl_vfs_wait() cannot block a worker either; a regression that made it block would hang here. Its guard
    // only logs, so the flag below pins the close as a genuinely in-flight one rather than the wait itself.
    fl_vfs_wait(ctx->handle);
    atomic_store(&ctx->ready_during_wait, fl_vfs_is_ready(ctx->handle));

    fl_vfs_close(ctx->handle);
    atomic_store(&ctx->close_attempted, true);
    return (FlVfsData) { .data = data, .size = size, .success = true };
}

static void vfs_worker_close_release(void* user_data) {
    VfsWorkerCloseCtx* ctx = (VfsWorkerCloseCtx*)user_data;
    atomic_store(&ctx->released_after_close, atomic_load(&ctx->close_attempted));
    atomic_fetch_add(&ctx->releases, 1);
}

UTEST(vfs, close_from_worker_takes_effect_and_the_running_job_still_finishes) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    vfs_wait_all();
    ASSERT_TRUE(fl_vfs_mount_is_ready(mount.mount));

    static VfsWorkerCloseCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    atomic_init(&ctx.ready_during_wait, true);
    atomic_init(&ctx.close_attempted, false);
    atomic_init(&ctx.releases, 0);
    atomic_init(&ctx.released_after_close, false);
    ctx.handle = vfs_mount_read_all_prepare(mount.mount, FL_S(STUB_VFS_FILE), vfs_worker_close_callback, &ctx,
                                            vfs_worker_close_release, FL_VFS_HANDLE_INVALID);
    ASSERT_NE(ctx.handle, FL_VFS_HANDLE_INVALID);
    vfs_dispatch(ctx.handle);

    // The close retires the id, so fl_vfs_wait(id) is a no-op here: the release hook is what reports the job is over.
    u64 deadline_ns = get_monotonic_time_ns_os() + 5000000000ull;
    while (atomic_load(&ctx.releases) == 0 && get_monotonic_time_ns_os() < deadline_ns) {
        sleep_ms(1);
    }

    ASSERT_TRUE_MSG(atomic_load(&ctx.close_attempted), "the callback must have run and closed its own handle");
    ASSERT_FALSE_MSG(atomic_load(&ctx.ready_during_wait),
                     "the callback must have run with its own read still in flight");
    ASSERT_FALSE_MSG(fl_vfs_is_ready(ctx.handle), "a close from a worker must retire the id at once");
    ASSERT_FALSE(fl_vfs_get_data(ctx.handle).success);
    ASSERT_EQ_MSG(atomic_load(&ctx.releases), 1, "the hook runs once, when the job ends");
    ASSERT_TRUE_MSG(atomic_load(&ctx.released_after_close), "the hook must not run before the job's callback is done");

    fl_vfs_close(ctx.handle); // a stale id: nothing to do
    vfs_wait_all();
    ASSERT_EQ_MSG(atomic_load(&ctx.releases), 1, "a second close and a wait_all must not release again");

    fl_vfs_mount_close(mount.mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A read issued from a worker against a mount that is still coming up is scheduled behind that mount's own
// job, so the worker's wait cannot make it ready. Its close from the worker takes effect all the same, and the read
// still runs once the mount is up. unit_tests runs a one-worker job system: parking that worker in a read-all
// callback is what keeps the second mount below pending.

typedef struct VfsPendingMountCtx {
    _Atomic(FlVfsMount*) mount;
    _Atomic bool in_callback;
    _Atomic bool setup_done;
    _Atomic FlVfsHandle ticket;
    _Atomic bool mount_pending_at_launch;
    _Atomic bool ready_after_wait;
    _Atomic bool probe_done;
    _Atomic bool release;
    _Atomic int ticket_callbacks;
    _Atomic int ticket_releases;
    _Atomic bool ticket_bytes_matched;
    _Atomic bool ticket_released_before_callback;
} VfsPendingMountCtx;

static FlVfsData vfs_pending_ticket_callback(void* data, i64 size, void* user_data) {
    VfsPendingMountCtx* ctx = (VfsPendingMountCtx*)user_data;
    atomic_store(&ctx->ticket_bytes_matched,
                 size == (i64)sizeof(k_stub_vfs_data) && memcmp(data, k_stub_vfs_data, sizeof(k_stub_vfs_data)) == 0);
    atomic_fetch_add(&ctx->ticket_callbacks, 1);
    return (FlVfsData) { .data = data, .size = size, .success = true };
}

static void vfs_pending_ticket_release(void* user_data) {
    VfsPendingMountCtx* ctx = (VfsPendingMountCtx*)user_data;
    if (atomic_load(&ctx->ticket_callbacks) == 0) {
        atomic_store(&ctx->ticket_released_before_callback, true);
    }
    atomic_fetch_add(&ctx->ticket_releases, 1);
}

static FlVfsData vfs_pending_mount_callback(void* data, i64 size, void* user_data) {
    VfsPendingMountCtx* ctx = (VfsPendingMountCtx*)user_data;
    atomic_store(&ctx->in_callback, true);

    (void)vfs_await_flag(&ctx->setup_done);

    FlVfsMount* mount = atomic_load(&ctx->mount);
    if (mount != nullptr) {
        atomic_store(&ctx->mount_pending_at_launch, !fl_vfs_mount_is_ready(mount));

        FlVfsHandle ticket = fl_vfs_mount_read_all_with_options(mount, FL_S(STUB_VFS_FILE), vfs_pending_ticket_callback,
                                                                ctx, vfs_pending_ticket_release, FL_VFS_HANDLE_INVALID);
        atomic_store(&ctx->ticket, ticket);

        fl_vfs_wait(ticket);
        atomic_store(&ctx->ready_after_wait, fl_vfs_is_ready(ticket));
        fl_vfs_close(ticket);
    }

    atomic_store(&ctx->probe_done, true);
    (void)vfs_await_flag(&ctx->release);

    return (FlVfsData) { .data = data, .size = size, .success = true };
}

UTEST(vfs, worker_read_against_a_pending_mount_is_deferred_and_still_runs_after_its_close) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    FlVfsMountResult ready_mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(ready_mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    vfs_wait_all();
    ASSERT_TRUE(fl_vfs_mount_is_ready(ready_mount.mount));

    static VfsPendingMountCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    atomic_init(&ctx.mount, nullptr);
    atomic_init(&ctx.in_callback, false);
    atomic_init(&ctx.setup_done, false);
    atomic_init(&ctx.ticket, FL_VFS_HANDLE_INVALID);
    atomic_init(&ctx.mount_pending_at_launch, false);
    atomic_init(&ctx.ready_after_wait, true);
    atomic_init(&ctx.probe_done, false);
    atomic_init(&ctx.release, false);
    atomic_init(&ctx.ticket_callbacks, 0);
    atomic_init(&ctx.ticket_releases, 0);
    atomic_init(&ctx.ticket_bytes_matched, false);
    atomic_init(&ctx.ticket_released_before_callback, false);

    FlVfsHandle parker = vfs_mount_read_all_prepare(ready_mount.mount, FL_S(STUB_VFS_FILE), vfs_pending_mount_callback,
                                                    &ctx, NULL, FL_VFS_HANDLE_INVALID);
    ASSERT_NE(parker, FL_VFS_HANDLE_INVALID);
    vfs_dispatch(parker);

    // Every wait below is deadlined and breaks rather than asserting, and the release is unconditional:
    // from here on the only worker is parked in the callback, and an ASSERT returns from the case without
    // releasing it, which would leave vfs_destroy's wait with nothing to wait on but a spinning worker.
    bool callback_started = true;
    u64 deadline_ns = get_monotonic_time_ns_os() + 5000000000ull;
    while (!atomic_load(&ctx.in_callback)) {
        if (get_monotonic_time_ns_os() >= deadline_ns) {
            callback_started = false;
            break;
        }
        sleep_ms(1);
    }

    FlVfsMountResult pending_mount = vfs_mount(FL_S("/stub"));
    atomic_store(&ctx.mount, pending_mount.status == FlVfsMountErrorStatus_Success ? pending_mount.mount : nullptr);
    atomic_store(&ctx.setup_done, true);

    bool probe_finished = true;
    deadline_ns = get_monotonic_time_ns_os() + 10000000000ull;
    while (!atomic_load(&ctx.probe_done)) {
        if (get_monotonic_time_ns_os() >= deadline_ns) {
            probe_finished = false;
            break;
        }
        sleep_ms(1);
    }
    atomic_store(&ctx.release, true);

    fl_vfs_wait(parker);
    vfs_wait_all(); // lets the second mount come up and the read deferred behind it run

    ASSERT_TRUE_MSG(callback_started, "the read-all callback never ran");
    ASSERT_TRUE_MSG(probe_finished, "the worker never finished reading from the pending mount");
    ASSERT_EQ_MSG(pending_mount.status, FlVfsMountErrorStatus_Success, "second stub mount should succeed");
    ASSERT_TRUE_MSG(atomic_load(&ctx.mount_pending_at_launch),
                    "the second mount must still have been coming up when the worker read from it");
    ASSERT_FALSE_MSG(atomic_load(&ctx.ready_after_wait),
                     "a read deferred behind a pending mount must not be ready after a worker's no-op wait");

    // The close took effect on the worker, and the read it left behind still ran once the mount was up: its callback
    // once, then its release hook once.
    FlVfsHandle ticket = atomic_load(&ctx.ticket);
    ASSERT_NE(ticket, FL_VFS_HANDLE_INVALID);
    ASSERT_FALSE_MSG(fl_vfs_is_ready(ticket), "the worker's close must have retired the ticket");
    ASSERT_EQ_MSG(atomic_load(&ctx.ticket_callbacks), 1, "the deferred read must run once the mount is up");
    ASSERT_EQ_MSG(atomic_load(&ctx.ticket_releases), 1, "the closed read must release its user_data once it ran");
    ASSERT_TRUE_MSG(atomic_load(&ctx.ticket_bytes_matched), "the deferred read must deliver the file's bytes");
    ASSERT_FALSE_MSG(atomic_load(&ctx.ticket_released_before_callback), "the release must follow the callback");

    fl_vfs_close(parker);
    fl_vfs_mount_close(pending_mount.mount);
    fl_vfs_mount_close(ready_mount.mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A worker runs a chained read inline because it cannot wait for the op it chains onto. Against an open that
// has not run yet that would read a file handle whose plugin_file_handle is still null ("File not opened"),
// so the chain must refuse instead. unit_tests runs a one-worker job system: parking that worker in a
// read-all callback keeps the open below queued for as long as the callback holds it.

typedef struct VfsWorkerChainCtx {
    _Atomic FlVfsHandle file;
    _Atomic bool file_ready; // set once file holds what the open returned
    _Atomic bool in_callback;
    _Atomic VfsOpStatus chained_status;
    _Atomic bool chain_done;
} VfsWorkerChainCtx;

static FlVfsData vfs_worker_chain_callback(void* data, i64 size, void* user_data) {
    VfsWorkerChainCtx* ctx = (VfsWorkerChainCtx*)user_data;
    atomic_store(&ctx->in_callback, true);

    (void)vfs_await_flag(&ctx->file_ready);
    FlVfsHandle file = atomic_load(&ctx->file);

    u8 buffer[sizeof(k_stub_vfs_data)] = { 0 };
    FlVfsHandle read = fl_vfs_read(file, buffer, sizeof(buffer));
    atomic_store(&ctx->chained_status, vfs_get_status(read));
    fl_vfs_close(read);
    atomic_store(&ctx->chain_done, true);

    return (FlVfsData) { .data = data, .size = size, .success = true };
}

UTEST(vfs, chained_read_from_worker_refuses_unfinished_open) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    vfs_wait_all();
    ASSERT_TRUE(fl_vfs_mount_is_ready(mount.mount));

    static VfsWorkerChainCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    atomic_init(&ctx.file, FL_VFS_HANDLE_INVALID);
    atomic_init(&ctx.file_ready, false);
    atomic_init(&ctx.in_callback, false);
    atomic_init(&ctx.chained_status, VFS_STATE_PENDING);
    atomic_init(&ctx.chain_done, false);

    FlVfsHandle reader = vfs_mount_read_all_prepare(mount.mount, FL_S(STUB_VFS_FILE), vfs_worker_chain_callback, &ctx,
                                                    NULL, FL_VFS_HANDLE_INVALID);
    ASSERT_NE(reader, FL_VFS_HANDLE_INVALID);
    vfs_dispatch(reader);

    // The worker is parked until ctx.file is stored, so nothing may ASSERT before it is: findings go into locals.
    const bool parked = vfs_await_flag(&ctx.in_callback);

    FlVfsHandle file = fl_vfs_mount_open(mount.mount, FL_S(STUB_VFS_FILE), FlVfsOpenFlags_Read);
    const bool still_queued = file != FL_VFS_HANDLE_INVALID && !fl_vfs_is_ready(file);
    atomic_store(&ctx.file, file);
    atomic_store(&ctx.file_ready, true);

    const bool chained = vfs_await_flag(&ctx.chain_done);

    ASSERT_TRUE_MSG(parked, "the read-all callback must park the only worker");
    ASSERT_NE(file, FL_VFS_HANDLE_INVALID);
    ASSERT_TRUE_MSG(still_queued, "the open must still be queued behind the parked worker");
    ASSERT_TRUE_MSG(chained, "the parked worker never attempted the chained read");
    ASSERT_EQ_MSG(atomic_load(&ctx.chained_status), (VfsOpStatus)VFS_ERROR_WOULD_BLOCK,
                  "chaining onto an unfinished open from a worker must refuse, not read an unopened file");

    fl_vfs_wait(reader);
    ASSERT_TRUE(fl_vfs_get_data(reader).success);
    fl_vfs_close(reader);

    // The refusal left the open untouched: it still completes, and still serves a read from the main thread.
    u8 buffer[sizeof(k_stub_vfs_data)] = { 0 };
    FlVfsHandle read = fl_vfs_read(file, buffer, sizeof(buffer));
    ASSERT_NE(read, FL_VFS_HANDLE_INVALID);
    fl_vfs_wait(read);
    FlVfsData data = fl_vfs_get_data(read);
    ASSERT_TRUE_MSG(data.success, "the open a refused chain left alone must still read");
    ASSERT_EQ(memcmp(buffer, k_stub_vfs_data, sizeof(k_stub_vfs_data)), 0);

    fl_vfs_close(read);
    fl_vfs_close(file);
    fl_vfs_mount_close(mount.mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// unit_tests runs a one-worker job system: parking that worker in a read-all callback keeps everything issued
// after it queued until the case lets it go.

typedef struct VfsParkCtx {
    _Atomic bool in_callback;
    _Atomic bool release;
} VfsParkCtx;

static FlVfsData vfs_park_callback(void* data, i64 size, void* user_data) {
    VfsParkCtx* ctx = (VfsParkCtx*)user_data;
    atomic_store(&ctx->in_callback, true);
    (void)vfs_await_flag(&ctx->release);
    return (FlVfsData) { .data = data, .size = size, .success = true };
}

// Parks the only worker in parker's callback; false if it never got there. The caller stores ctx->release
// unconditionally before its first ASSERT: an ASSERT returns from the case, and a worker still parked then
// leaves vfs_destroy's wait with nothing to wait on but a spinning worker.
static bool vfs_park_worker(VfsParkCtx* ctx, FlVfsMount* mount, FlVfsHandle* parker, FlString path) {
    atomic_init(&ctx->in_callback, false);
    atomic_init(&ctx->release, false);
    *parker = vfs_mount_read_all_prepare(mount, path, vfs_park_callback, ctx, NULL, FL_VFS_HANDLE_INVALID);
    if (*parker == FL_VFS_HANDLE_INVALID) {
        return false;
    }
    vfs_dispatch(*parker);
    u64 deadline_ns = get_monotonic_time_ns_os() + 5000000000ull;
    while (!atomic_load(&ctx->in_callback)) {
        if (get_monotonic_time_ns_os() >= deadline_ns) {
            return false;
        }
        sleep_ms(1);
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A read chained onto a file holds a reference to it from creation until it has run. This drives a close into
// that span: the worker parks in a read-all callback and chains a read onto an already-finished open from there,
// the gated stub read holds the read open, and the main thread closes the file handle while it is held. The
// close gives up the id but must not free the file - or the plugin_file_handle the blocked read is still using.

typedef struct VfsChainRaceCtx {
    _Atomic bool in_callback;
    _Atomic bool start_chain;
    _Atomic bool release;
    _Atomic FlVfsHandle file;
    _Atomic int chained_status;
    _Atomic bool chained_matched;
    _Atomic bool chain_done;
} VfsChainRaceCtx;

static FlVfsData vfs_chain_race_callback(void* data, i64 size, void* user_data) {
    VfsChainRaceCtx* ctx = (VfsChainRaceCtx*)user_data;
    atomic_store(&ctx->in_callback, true);

    (void)vfs_await_flag(&ctx->start_chain);

    // Runs inline on this worker, so the whole read happens inside the unlocked window.
    u8 buffer[sizeof(k_stub_vfs_data)] = { 0 };
    FlVfsHandle read = fl_vfs_read(atomic_load(&ctx->file), buffer, sizeof(buffer));
    atomic_store(&ctx->chained_status, (int)vfs_get_status(read));
    atomic_store(&ctx->chained_matched, memcmp(buffer, k_stub_vfs_data, sizeof(k_stub_vfs_data)) == 0);
    fl_vfs_close(read);
    atomic_store(&ctx->chain_done, true);

    (void)vfs_await_flag(&ctx->release);
    return (FlVfsData) { .data = data, .size = size, .success = true };
}

UTEST(vfs, close_racing_a_chained_read_defers_instead_of_freeing_the_file_handle) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    vfs_wait_all();

    // The open must finish before the worker parks: a chain onto an unfinished open is refused outright, and
    // it is the finished predecessor that makes the pre-guard close believe the handle is idle.
    FlVfsHandle file = fl_vfs_mount_open(mount.mount, FL_S(STUB_VFS_FILE), FlVfsOpenFlags_Read);
    ASSERT_NE(file, FL_VFS_HANDLE_INVALID);
    fl_vfs_wait(file);
    ASSERT_TRUE(fl_vfs_is_ready(file));

    static VfsChainRaceCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    atomic_init(&ctx.in_callback, false);
    atomic_init(&ctx.start_chain, false);
    atomic_init(&ctx.release, false);
    atomic_init(&ctx.file, file);
    atomic_init(&ctx.chained_status, (int)VFS_STATE_PENDING);
    atomic_init(&ctx.chained_matched, false);
    atomic_init(&ctx.chain_done, false);

    FlVfsHandle parker = vfs_mount_read_all_prepare(mount.mount, FL_S(STUB_VFS_FILE), vfs_chain_race_callback, &ctx,
                                                    NULL, FL_VFS_HANDLE_INVALID);
    ASSERT_NE(parker, FL_VFS_HANDLE_INVALID);
    vfs_dispatch(parker);

    // From here the worker is parked, so nothing may ASSERT until it is released below: an ASSERT returns
    // from the case and leaves teardown waiting on a worker that never comes back. Findings go into locals.
    bool parked = vfs_await_flag(&ctx.in_callback);

    // Arm the gate only now: the read-all's own read of the file has already gone through stub_vfs_read.
    atomic_store(&s_stub_vfs_read_entered, false);
    atomic_store(&s_stub_vfs_read_release, false);
    atomic_store(&s_stub_vfs_read_gate_armed, true);
    atomic_store(&ctx.start_chain, true);

    bool in_window = parked && vfs_await_flag(&s_stub_vfs_read_entered);

    // The worker is now blocked in the driver's read, through the file the main thread is about to close.
    const int closes_before = s_stub_vfs_close_count;
    if (in_window) {
        fl_vfs_close(file);
    }
    const int closes_during = s_stub_vfs_close_count - closes_before;

    atomic_store(&s_stub_vfs_read_release, true);
    bool chained = in_window && vfs_await_flag(&ctx.chain_done);
    atomic_store(&s_stub_vfs_read_gate_armed, false);
    atomic_store(&ctx.release, true);
    vfs_wait_all();

    ASSERT_TRUE_MSG(parked, "the read-all callback must park the only worker");
    ASSERT_TRUE_MSG(in_window, "the chained read must reach the driver and hold the scheduling window open");
    ASSERT_TRUE_MSG(chained, "the chained read must complete once the gate is released");

    ASSERT_EQ_MSG(closes_during, 0,
                  "a close landing while a chained read runs must defer: freeing there closes the plugin file handle "
                  "the chained read is still reading through");
    ASSERT_EQ_MSG(atomic_load(&ctx.chained_status), (int)VFS_STATE_SUCCESS,
                  "the read chained across the close must still complete against a live file handle");
    ASSERT_TRUE_MSG(atomic_load(&ctx.chained_matched), "the chained read must deliver the file's bytes");

    // Deferred, not cancelled: the read hands the file back when it ends, which frees it, and the driver's file
    // handle, exactly once.
    ASSERT_EQ_MSG(s_stub_vfs_close_count - closes_before, 1,
                  "the deferred close must still release the plugin file handle exactly once");

    fl_vfs_mount_close(mount.mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// An op whose dispatch has not returned yet has no job handle recorded, so a mount close has nothing to wait on
// for it; what keeps the close from freeing it is the job's own reference. This drives that window: the only worker
// parks in a read-all on a second mount and dispatches a read-all on the mount under test from there, which runs
// inline and blocks in the gated stub read while the dispatch is still unrecorded. The close must neither free
// the op, release hook and all, nor return, while the job is still reading through it.

typedef struct VfsDispatchRaceCtx {
    FlVfsMount* target; // the mount the close under test tears down
    _Atomic bool in_callback;
    _Atomic bool start_dispatch;
    _Atomic bool dispatch_returned;
    _Atomic bool close_started;
    _Atomic bool freed_in_flight; // the victim's release hook ran while its read was still blocked
    _Atomic bool victim_released; // the victim's release hook has run at all
    _Atomic int victim_callback_calls;
    _Atomic bool victim_bytes_matched;
    _Atomic bool release_parker;
} VfsDispatchRaceCtx;

static FlVfsData vfs_dispatch_race_victim_callback(void* data, i64 size, void* user_data) {
    VfsDispatchRaceCtx* ctx = (VfsDispatchRaceCtx*)user_data;
    atomic_fetch_add(&ctx->victim_callback_calls, 1);
    atomic_store(&ctx->victim_bytes_matched,
                 size == (i64)sizeof(k_stub_vfs_data) && memcmp(data, k_stub_vfs_data, sizeof(k_stub_vfs_data)) == 0);
    return (FlVfsData) { .data = data, .size = size, .success = true };
}

static void vfs_dispatch_race_victim_release(void* user_data) {
    VfsDispatchRaceCtx* ctx = (VfsDispatchRaceCtx*)user_data;
    if (!atomic_load(&s_stub_vfs_read_release)) {
        atomic_store(&ctx->freed_in_flight, true);
    }
    atomic_store(&ctx->victim_released, true);
}

// Runs on the only worker: scheduling from there runs the dispatched job inline, so vfs_dispatch stays in
// its unlocked window for as long as the gated stub read holds that job.
static FlVfsData vfs_dispatch_race_callback(void* data, i64 size, void* user_data) {
    VfsDispatchRaceCtx* ctx = (VfsDispatchRaceCtx*)user_data;
    atomic_store(&ctx->in_callback, true);

    (void)vfs_await_flag(&ctx->start_dispatch);

    FlVfsHandle victim = vfs_mount_read_all_prepare(ctx->target, FL_S(STUB_VFS_FILE), vfs_dispatch_race_victim_callback,
                                                    ctx, vfs_dispatch_race_victim_release, FL_VFS_HANDLE_INVALID);
    vfs_dispatch(victim);
    atomic_store(&ctx->dispatch_returned, true);

    (void)vfs_await_flag(&ctx->release_parker);
    return (FlVfsData) { .data = data, .size = size, .success = true };
}

// Lets the gated read go once the close has had its chance to free the victim early. The main thread cannot
// do it itself: with the guard in place its fl_vfs_mount_close is inside the wait this releases.
static void* vfs_dispatch_race_releaser(void* user_data) {
    VfsDispatchRaceCtx* ctx = (VfsDispatchRaceCtx*)user_data;

    if (vfs_await_flag(&ctx->close_started)) {
        u64 deadline_ns = get_monotonic_time_ns_os() + 100000000ull;
        while (!atomic_load(&ctx->freed_in_flight) && get_monotonic_time_ns_os() < deadline_ns) {
            sleep_ms(1);
        }
    }

    atomic_store(&s_stub_vfs_read_release, true);
    return nullptr;
}

UTEST(vfs, mount_close_waits_out_a_dispatch_that_has_not_recorded_its_job) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    // Two mounts: the close under test tears down target, while the parked worker's own read-all lives on
    // host, which that close neither drains nor frees.
    FlVfsMountResult target = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(target.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    FlVfsMountResult host = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(host.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    vfs_wait_all();
    ASSERT_TRUE(fl_vfs_mount_is_ready(target.mount));
    ASSERT_TRUE(fl_vfs_mount_is_ready(host.mount));

    VfsDispatchRaceCtx ctx = { .target = target.mount };
    atomic_init(&ctx.in_callback, false);
    atomic_init(&ctx.start_dispatch, false);
    atomic_init(&ctx.dispatch_returned, false);
    atomic_init(&ctx.close_started, false);
    atomic_init(&ctx.freed_in_flight, false);
    atomic_init(&ctx.victim_released, false);
    atomic_init(&ctx.victim_callback_calls, 0);
    atomic_init(&ctx.victim_bytes_matched, false);
    atomic_init(&ctx.release_parker, false);

    FlVfsHandle parker = vfs_mount_read_all_prepare(host.mount, FL_S(STUB_VFS_FILE), vfs_dispatch_race_callback, &ctx,
                                                    NULL, FL_VFS_HANDLE_INVALID);
    ASSERT_NE(parker, FL_VFS_HANDLE_INVALID);
    vfs_dispatch(parker);

    // From here the only worker is parked, so nothing may ASSERT until it is released below: an ASSERT
    // returns from the case and leaves teardown waiting on a worker that never comes back. Findings go into
    // locals.
    bool parked = vfs_await_flag(&ctx.in_callback);

    // Arm the gate only now: the parker's own read of the file has already gone through stub_vfs_read.
    atomic_store(&s_stub_vfs_read_entered, false);
    atomic_store(&s_stub_vfs_read_release, false);
    atomic_store(&s_stub_vfs_read_gate_armed, true);
    atomic_store(&ctx.start_dispatch, true);

    bool in_window = parked && vfs_await_flag(&s_stub_vfs_read_entered);

    bool drained = false;
    Thread releaser = { 0 };
    bool releasing = in_window && os_thread_create(&releaser, vfs_dispatch_race_releaser, &ctx);

    if (releasing) {
        atomic_store(&ctx.close_started, true);
        fl_vfs_mount_close(target.mount);
        // The main-thread close waits for the victim's job and its hook before it returns.
        drained = atomic_load(&ctx.victim_released) && atomic_load(&ctx.victim_callback_calls) == 1;
    }

    bool dispatch_returned = releasing && vfs_await_flag(&ctx.dispatch_returned);

    atomic_store(&s_stub_vfs_read_release, true);
    atomic_store(&s_stub_vfs_read_gate_armed, false);
    atomic_store(&ctx.release_parker, true);
    if (releasing) {
        os_thread_join(&releaser);
    }
    fl_vfs_wait(parker);
    fl_vfs_close(parker);
    if (!releasing) {
        fl_vfs_mount_close(target.mount); // the case bailed before the close under test could run it
    }
    fl_vfs_mount_close(host.mount);

    ASSERT_TRUE_MSG(parked, "the read-all callback must park the only worker");
    ASSERT_TRUE_MSG(in_window, "the dispatched read must reach the driver and hold the scheduling window open");
    ASSERT_TRUE_MSG(releasing, "the releaser thread must start; the close under test blocks without it");
    ASSERT_TRUE_MSG(dispatch_returned, "the dispatch must record its job and return");

    ASSERT_TRUE_MSG(drained,
                    "fl_vfs_mount_close on the main thread must not return before the victim has run and released");
    ASSERT_FALSE_MSG(atomic_load(&ctx.freed_in_flight),
                     "fl_vfs_mount_close must not free a victim whose dispatch has not recorded its job yet: the "
                     "job is still running against the handle");
    ASSERT_EQ_MSG(atomic_load(&ctx.victim_callback_calls), 1,
                  "the dispatched read must run to completion against a live handle");
    ASSERT_TRUE_MSG(atomic_load(&ctx.victim_bytes_matched), "the dispatched read must deliver the file's bytes");
}

// A read-all's callback and release hook are counted separately, so a case can pin when the VFS reaches each and
// that the release comes after the callback: it is what tells user_data's owner the worker is done with it.
typedef struct VfsReleaseCtx {
    _Atomic int callback_calls;
    _Atomic int release_calls;
    _Atomic bool released_before_callback;
} VfsReleaseCtx;

static FlVfsData vfs_count_callback(void* data, i64 size, void* user_data) {
    VfsReleaseCtx* ctx = (VfsReleaseCtx*)user_data;
    atomic_fetch_add(&ctx->callback_calls, 1);
    return (FlVfsData) { .data = data, .size = size, .success = true };
}

static void vfs_count_release(void* user_data) {
    VfsReleaseCtx* ctx = (VfsReleaseCtx*)user_data;
    if (atomic_load(&ctx->callback_calls) == 0) {
        atomic_store(&ctx->released_before_callback, true);
    }
    atomic_fetch_add(&ctx->release_calls, 1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// fl_vfs_close() of a handle whose job has not run returns at once, leaves the job to finish on its own, and the
// op is released by the job when it ends. A close that waited would wait on a gate this thread opens only after the
// close has returned.

UTEST(vfs, close_of_a_queued_read_returns_and_the_job_releases_it) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    vfs_wait_all();
    ASSERT_TRUE(fl_vfs_mount_is_ready(mount.mount));

    VfsParkCtx park;
    FlVfsHandle parker;
    bool parked = vfs_park_worker(&park, mount.mount, &parker, FL_S(STUB_VFS_FILE));

    static VfsReleaseCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    atomic_init(&ctx.callback_calls, 0);
    atomic_init(&ctx.release_calls, 0);
    atomic_init(&ctx.released_before_callback, false);
    FlVfsHandle victim = fl_vfs_mount_read_all_with_options(mount.mount, FL_S(STUB_VFS_FILE), vfs_count_callback, &ctx,
                                                            vfs_count_release, FL_VFS_HANDLE_INVALID);

    // The gate: the worker is parked, so the read cannot have run. A close that waited for it would never
    // return, since the release below runs on this thread after it.
    fl_vfs_close(victim);
    int calls_at_close = atomic_load(&ctx.callback_calls);
    int releases_at_close = atomic_load(&ctx.release_calls);
    bool absent_after_close = vfs_get_status(victim) == VFS_STATE_ERROR;

    atomic_store(&park.release, true);
    fl_vfs_wait(parker);

    // The victim's job runs once the worker is free, and releases the op as it ends. Nothing here closes or waits
    // on anything else. Deadlined rather than asserted so the parker is never left parked.
    bool released_by_job = false;
    u64 deadline_ns = get_monotonic_time_ns_os() + 5000000000ull;
    while (get_monotonic_time_ns_os() < deadline_ns) {
        if (atomic_load(&ctx.release_calls) == 1) {
            released_by_job = true;
            break;
        }
        sleep_ms(1);
    }

    ASSERT_TRUE_MSG(parked, "the parking callback never ran");
    ASSERT_NE(victim, FL_VFS_HANDLE_INVALID);
    ASSERT_EQ_MSG(calls_at_close, 0, "the close must return before the queued read has run");
    ASSERT_EQ_MSG(releases_at_close, 0, "user_data must not be released while the job can still reach it");
    ASSERT_TRUE_MSG(released_by_job, "the job must release the closed op when it ends");
    ASSERT_TRUE_MSG(absent_after_close, "a closed handle must read as absent at once");
    ASSERT_EQ_MSG(atomic_load(&ctx.callback_calls), 1, "the read's callback must run exactly once");
    ASSERT_EQ_MSG(atomic_load(&ctx.release_calls), 1, "user_data must be released exactly once");
    ASSERT_FALSE_MSG(atomic_load(&ctx.released_before_callback), "the release must follow the callback");

    // A finished read releases at its close, on the closer's thread.
    static VfsReleaseCtx finished;
    memset(&finished, 0, sizeof(finished));
    atomic_init(&finished.callback_calls, 0);
    atomic_init(&finished.release_calls, 0);
    atomic_init(&finished.released_before_callback, false);
    FlVfsHandle done = fl_vfs_mount_read_all_with_options(mount.mount, FL_S(STUB_VFS_FILE), vfs_count_callback,
                                                          &finished, vfs_count_release, FL_VFS_HANDLE_INVALID);
    ASSERT_NE(done, FL_VFS_HANDLE_INVALID);
    fl_vfs_wait(done);
    ASSERT_EQ(atomic_load(&finished.release_calls), 0);
    fl_vfs_close(done);
    ASSERT_EQ_MSG(atomic_load(&finished.release_calls), 1, "an immediate close releases user_data before returning");
    ASSERT_FALSE(atomic_load(&finished.released_before_callback));

    // A handle never closed is reaped by its mount's close, release hook included: the path every leaked
    // read takes at shutdown.
    static VfsReleaseCtx leaked;
    memset(&leaked, 0, sizeof(leaked));
    atomic_init(&leaked.callback_calls, 0);
    atomic_init(&leaked.release_calls, 0);
    atomic_init(&leaked.released_before_callback, false);
    FlVfsHandle never_closed = fl_vfs_mount_read_all_with_options(mount.mount, FL_S(STUB_VFS_FILE), vfs_count_callback,
                                                                  &leaked, vfs_count_release, FL_VFS_HANDLE_INVALID);
    ASSERT_NE(never_closed, FL_VFS_HANDLE_INVALID);

    fl_vfs_close(parker);
    fl_vfs_mount_close(mount.mount);
    ASSERT_EQ_MSG(atomic_load(&leaked.callback_calls), 1, "the mount close drained the leaked read");
    ASSERT_EQ_MSG(atomic_load(&leaked.release_calls), 1, "the mount close released the leaked read's user_data once");
    ASSERT_FALSE(atomic_load(&leaked.released_before_callback));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Closing a file handle with a read chained onto it returns at once as well; the file stays alive until the
// read has run, the read still gets its bytes, and nothing chains onto the closed file afterwards.

UTEST(vfs, close_of_a_queued_file_handle_returns_and_its_chained_read_still_completes) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    vfs_wait_all();
    ASSERT_TRUE(fl_vfs_mount_is_ready(mount.mount));

    VfsParkCtx park;
    FlVfsHandle parker;
    bool parked = vfs_park_worker(&park, mount.mount, &parker, FL_S(STUB_VFS_FILE));

    FlVfsHandle file = fl_vfs_mount_open(mount.mount, FL_S(STUB_VFS_FILE), FlVfsOpenFlags_Read); // queued behind parker
    u8 buf[sizeof(k_stub_vfs_data)] = { 0 };
    FlVfsHandle read = fl_vfs_read(file, buf, sizeof(buf)); // chained onto the queued open

    fl_vfs_close(file); // returns: the open has not run
    bool file_absent_after_close = !fl_vfs_is_ready(file);
    u8 late_buf[sizeof(k_stub_vfs_data)] = { 0 };
    FlVfsHandle late_read = fl_vfs_read(file, late_buf, sizeof(late_buf));

    atomic_store(&park.release, true);
    fl_vfs_wait(parker);
    fl_vfs_wait(read);

    ASSERT_TRUE_MSG(parked, "the parking callback never ran");
    ASSERT_NE(file, FL_VFS_HANDLE_INVALID);
    ASSERT_NE(read, FL_VFS_HANDLE_INVALID);
    ASSERT_TRUE_MSG(file_absent_after_close, "a closed file handle must read as absent at once");
    ASSERT_EQ_MSG(late_read, FL_VFS_HANDLE_INVALID, "nothing chains onto a closed file handle");

    FlVfsData data = fl_vfs_get_data(read);
    ASSERT_TRUE_MSG(data.success, "the open ran and its file stayed alive for the read chained before the close");
    ASSERT_EQ((size_t)data.size, sizeof(k_stub_vfs_data));
    ASSERT_EQ(memcmp(buf, k_stub_vfs_data, sizeof(k_stub_vfs_data)), 0);
    fl_vfs_close(read);

    vfs_wait_all(); // the chain has run, so the file is gone
    FlVfsHandle reused = vfs_mount_read_all_prepare(mount.mount, FL_S(STUB_VFS_FILE), NULL, NULL, NULL, file);
    ASSERT_EQ_MSG(reused, file, "a closed file id must be reusable");
    fl_vfs_close(reused);

    fl_vfs_close(parker);
    fl_vfs_mount_close(mount.mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Closing an id retires it, and the id stays reserved until the old op has finished and been destroyed: a reload
// under the same id must not race the closed op's completion. Reuse of an id that is still open, or retiring, is
// refused with a warning, and succeeds once the old op is gone.

UTEST(vfs, a_closed_id_stays_reserved_until_its_old_op_has_run) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    vfs_wait_all();
    ASSERT_TRUE(fl_vfs_mount_is_ready(mount.mount));

    VfsParkCtx park;
    FlVfsHandle parker;
    bool parked = vfs_park_worker(&park, mount.mount, &parker, FL_S(STUB_VFS_FILE));

    static VfsReleaseCtx first;
    memset(&first, 0, sizeof(first));
    static VfsReleaseCtx second;
    memset(&second, 0, sizeof(second));
    FlVfsHandle id
        = fl_vfs_mount_read_all_with_options(mount.mount, FL_S(STUB_VFS_FILE), vfs_count_callback, &first,
                                             vfs_count_release, FL_VFS_HANDLE_INVALID); // queued behind parker
    FlVfsHandle refused_open = vfs_mount_read_all_prepare(mount.mount, FL_S(STUB_VFS_FILE), NULL, NULL, NULL, id);
    fl_vfs_close(id);
    ASSERT_FALSE_MSG(fl_vfs_is_ready(id), "a closed id must read as absent at once");
    FlVfsHandle refused_retiring = vfs_mount_read_all_prepare(mount.mount, FL_S(STUB_VFS_FILE), NULL, NULL, NULL, id);

    atomic_store(&park.release, true);
    fl_vfs_wait(parker);
    vfs_wait_all(); // the closed op's job is waited for too, and releases the op as it ends

    FlVfsHandle reused = fl_vfs_mount_read_all_with_options(mount.mount, FL_S(STUB_VFS_FILE), vfs_count_callback,
                                                            &second, vfs_count_release, id);
    fl_vfs_wait(reused);

    ASSERT_TRUE_MSG(parked, "the parking callback never ran");
    ASSERT_NE(id, FL_VFS_HANDLE_INVALID);
    ASSERT_EQ_MSG(refused_open, FL_VFS_HANDLE_INVALID, "an id that is still open cannot be reused");
    ASSERT_EQ_MSG(refused_retiring, FL_VFS_HANDLE_INVALID, "a closed id whose op is still queued cannot be reused");
    ASSERT_EQ(atomic_load(&first.callback_calls), 1);
    ASSERT_EQ_MSG(atomic_load(&first.release_calls), 1, "the closed op releases its own user_data when its job ends");
    ASSERT_EQ_MSG(reused, id, "the id is free again once its old op is gone");
    ASSERT_EQ(atomic_load(&second.callback_calls), 1);
    ASSERT_EQ_MSG(atomic_load(&second.release_calls), 0, "the reusing op is still open");
    ASSERT_TRUE_MSG(fl_vfs_get_data(reused).success, "the id names the new op, not the closed one");

    fl_vfs_close(reused);
    ASSERT_EQ(atomic_load(&second.release_calls), 1);
    ASSERT_EQ(atomic_load(&first.release_calls), 1);

    fl_vfs_close(parker);
    fl_vfs_mount_close(mount.mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A read hands its file back as soon as it has run, not when its own id is closed: closing the file id afterwards
// closes the driver's file handle at once, while the read's id is still open.

UTEST(vfs, a_read_that_has_run_no_longer_keeps_its_file_open) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    vfs_wait_all();
    ASSERT_TRUE(fl_vfs_mount_is_ready(mount.mount));

    FlVfsHandle file = fl_vfs_mount_open(mount.mount, FL_S(STUB_VFS_FILE), FlVfsOpenFlags_Read);
    ASSERT_NE(file, FL_VFS_HANDLE_INVALID);
    u8 buffer[sizeof(k_stub_vfs_data)] = { 0 };
    FlVfsHandle read = fl_vfs_read(file, buffer, sizeof(buffer));
    ASSERT_NE(read, FL_VFS_HANDLE_INVALID);
    fl_vfs_wait(read);
    ASSERT_TRUE(fl_vfs_get_data(read).success);

    const int closes_before = s_stub_vfs_close_count;
    fl_vfs_close(file);
    ASSERT_EQ_MSG(s_stub_vfs_close_count - closes_before, 1, "the file closes with its id once no read can reach it");
    ASSERT_TRUE_MSG(fl_vfs_is_ready(read), "the read's own id is unaffected");

    fl_vfs_close(read);
    fl_vfs_mount_close(mount.mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A worker cannot wait, so a mount close from one returns at once instead of draining: it refuses new operations on
// the mount, and the mount is freed - its root handle closed - when the last operation holding it ends.

typedef struct VfsWorkerMountCloseCtx {
    FlVfsMount* mount;
    int allocs_before;
    int closes_before;
    _Atomic FlVfsHandle late;
    _Atomic bool closed_mount;
    _Atomic int open_handles_after_close;
} VfsWorkerMountCloseCtx;

static FlVfsData vfs_worker_mount_close_callback(void* data, i64 size, void* user_data) {
    VfsWorkerMountCloseCtx* ctx = (VfsWorkerMountCloseCtx*)user_data;

    fl_vfs_mount_close(ctx->mount);
    // The reader's own job still holds the mount, so its root handle is the one thing left open.
    atomic_store(&ctx->open_handles_after_close,
                 (s_stub_vfs_alloc_count - ctx->allocs_before) - (s_stub_vfs_close_count - ctx->closes_before));
    atomic_store(&ctx->late, fl_vfs_mount_read_all_with_options(ctx->mount, FL_S(STUB_VFS_FILE), NULL, NULL, NULL,
                                                                FL_VFS_HANDLE_INVALID));
    atomic_store(&ctx->closed_mount, true);

    return (FlVfsData) { .data = data, .size = size, .success = true };
}

UTEST(vfs, mount_close_from_a_worker_refuses_new_ops_and_frees_on_the_last_reference) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    const int allocs_before = s_stub_vfs_alloc_count;
    const int closes_before = s_stub_vfs_close_count;

    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    vfs_wait_all();
    ASSERT_TRUE(fl_vfs_mount_is_ready(mount.mount));

    VfsWorkerMountCloseCtx ctx
        = { .mount = mount.mount, .allocs_before = allocs_before, .closes_before = closes_before };
    atomic_init(&ctx.late, FL_VFS_HANDLE_INVALID + 1);
    atomic_init(&ctx.closed_mount, false);
    atomic_init(&ctx.open_handles_after_close, -1);
    FlVfsHandle reader = vfs_mount_read_all_prepare(mount.mount, FL_S(STUB_VFS_FILE), vfs_worker_mount_close_callback,
                                                    &ctx, NULL, FL_VFS_HANDLE_INVALID);
    ASSERT_NE(reader, FL_VFS_HANDLE_INVALID);
    vfs_dispatch(reader);

    // The reader's own job is the last thing holding the mount; the mount's root closes when that job ends.
    bool freed = false;
    u64 deadline_ns = get_monotonic_time_ns_os() + 5000000000ull;
    while (get_monotonic_time_ns_os() < deadline_ns) {
        if (atomic_load(&ctx.closed_mount)
            && s_stub_vfs_close_count - closes_before == s_stub_vfs_alloc_count - allocs_before) {
            freed = true;
            break;
        }
        sleep_ms(1);
    }

    ASSERT_TRUE_MSG(atomic_load(&ctx.closed_mount), "the callback must have closed the mount from the worker");
    ASSERT_EQ_MSG(atomic_load(&ctx.late), FL_VFS_HANDLE_INVALID, "a closing mount must refuse new operations");
    ASSERT_EQ_MSG(atomic_load(&ctx.open_handles_after_close), 1,
                  "a mount closed from a worker must keep its root open while an operation still holds it");
    ASSERT_GT(s_stub_vfs_alloc_count - allocs_before, 0);
    ASSERT_TRUE_MSG(freed, "the mount's root handle must close once its last operation has ended");
    ASSERT_FALSE_MSG(fl_vfs_is_ready(reader), "the mount close released the reader's id");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A mount closed from a worker leaves the mount list only when it is freed, so vfs_wait_all still waits for the ops
// it has running. Reader A (on the only worker) closes the mount from its callback; B was queued behind it before
// the close and runs against a closing mount. wait_all must not return before B has run and the root has closed.

typedef struct VfsListedCloseCtx {
    FlVfsMount* mount;
    _Atomic bool in_callback;
    _Atomic bool go;
    _Atomic bool closed_mount;
    _Atomic bool release;
} VfsListedCloseCtx;

static FlVfsData vfs_listed_close_callback(void* data, i64 size, void* user_data) {
    VfsListedCloseCtx* ctx = (VfsListedCloseCtx*)user_data;
    atomic_store(&ctx->in_callback, true);
    (void)vfs_await_flag(&ctx->go);
    fl_vfs_mount_close(ctx->mount);
    atomic_store(&ctx->closed_mount, true);
    (void)vfs_await_flag(&ctx->release);
    return (FlVfsData) { .data = data, .size = size, .success = true };
}

UTEST(vfs, wait_all_covers_the_ops_of_a_mount_closed_from_a_worker) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    const int allocs_before = s_stub_vfs_alloc_count;
    const int closes_before = s_stub_vfs_close_count;

    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    vfs_wait_all();

    VfsListedCloseCtx ctx = { .mount = mount.mount };
    atomic_init(&ctx.in_callback, false);
    atomic_init(&ctx.go, false);
    atomic_init(&ctx.closed_mount, false);
    atomic_init(&ctx.release, false);
    FlVfsHandle closer = vfs_mount_read_all_prepare(mount.mount, FL_S(STUB_VFS_FILE), vfs_listed_close_callback, &ctx,
                                                    NULL, FL_VFS_HANDLE_INVALID);
    vfs_dispatch(closer);
    bool parked = vfs_await_flag(&ctx.in_callback);

    static VfsReleaseCtx queued;
    memset(&queued, 0, sizeof(queued));
    FlVfsHandle behind = fl_vfs_mount_read_all_with_options(mount.mount, FL_S(STUB_VFS_FILE), vfs_count_callback,
                                                            &queued, vfs_count_release, FL_VFS_HANDLE_INVALID);

    // From here nothing may ASSERT until the worker is released.
    atomic_store(&ctx.go, true);
    bool closed = vfs_await_flag(&ctx.closed_mount);
    atomic_store(&ctx.release, true);
    vfs_wait_all();

    ASSERT_TRUE_MSG(parked, "the closing reader never ran");
    ASSERT_TRUE_MSG(closed, "the reader never closed the mount");
    ASSERT_NE(behind, FL_VFS_HANDLE_INVALID);
    ASSERT_EQ_MSG(atomic_load(&queued.callback_calls), 1,
                  "wait_all returned before the op queued on the closed mount ran");
    ASSERT_EQ(atomic_load(&queued.release_calls), 1);
    ASSERT_EQ_MSG(s_stub_vfs_close_count - closes_before, s_stub_vfs_alloc_count - allocs_before,
                  "wait_all returned before the closed mount had been freed");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// vfs_dispatch schedules an op once. Dispatching a prepared op twice runs it once, and dispatching a read that a worker
// already ran inline (its file reference handed back) must not run it again.

typedef struct VfsInlineReadCtx {
    _Atomic FlVfsHandle file;
    _Atomic FlVfsHandle read;
    u8 buffer[sizeof(k_stub_vfs_data)];
    _Atomic bool done;
} VfsInlineReadCtx;

static FlVfsData vfs_inline_read_callback(void* data, i64 size, void* user_data) {
    VfsInlineReadCtx* ctx = (VfsInlineReadCtx*)user_data;
    atomic_store(&ctx->read, fl_vfs_read(atomic_load(&ctx->file), ctx->buffer, sizeof(ctx->buffer)));
    atomic_store(&ctx->done, true);
    return (FlVfsData) { .data = data, .size = size, .success = true };
}

UTEST(vfs, dispatching_an_op_twice_runs_it_once) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    vfs_wait_all();

    static VfsReleaseCtx counts;
    memset(&counts, 0, sizeof(counts));
    FlVfsHandle twice = vfs_mount_read_all_prepare(mount.mount, FL_S(STUB_VFS_FILE), vfs_count_callback, &counts,
                                                   vfs_count_release, FL_VFS_HANDLE_INVALID);
    vfs_dispatch(twice);
    vfs_dispatch(twice);
    vfs_wait_all();
    fl_vfs_close(twice);
    ASSERT_EQ_MSG(atomic_load(&counts.callback_calls), 1, "a second dispatch must not run the op again");
    ASSERT_EQ(atomic_load(&counts.release_calls), 1);

    FlVfsHandle file = fl_vfs_mount_open(mount.mount, FL_S(STUB_VFS_FILE), FlVfsOpenFlags_Read);
    fl_vfs_wait(file);
    static VfsInlineReadCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    atomic_init(&ctx.file, file);
    atomic_init(&ctx.read, FL_VFS_HANDLE_INVALID);
    atomic_init(&ctx.done, false);
    FlVfsHandle reader = vfs_mount_read_all_prepare(mount.mount, FL_S(STUB_VFS_FILE), vfs_inline_read_callback, &ctx,
                                                    NULL, FL_VFS_HANDLE_INVALID);
    vfs_dispatch(reader);
    bool ran = vfs_await_flag(&ctx.done);
    FlVfsHandle read = atomic_load(&ctx.read);
    vfs_wait_all();
    const int reads_before_dispatch = s_stub_vfs_read_count;
    if (ran && read != FL_VFS_HANDLE_INVALID) {
        vfs_dispatch(read); // already ran inline on the worker
    }
    vfs_wait_all();

    ASSERT_TRUE_MSG(ran, "the reader never ran");
    ASSERT_NE(read, FL_VFS_HANDLE_INVALID);
    ASSERT_TRUE(fl_vfs_get_data(read).success);
    ASSERT_EQ(memcmp(ctx.buffer, k_stub_vfs_data, sizeof(k_stub_vfs_data)), 0);
    ASSERT_EQ_MSG(s_stub_vfs_read_count, reads_before_dispatch,
                  "dispatching a read a worker already ran must not read again");

    fl_vfs_close(read);
    fl_vfs_close(reader);
    fl_vfs_close(file);
    fl_vfs_mount_close(mount.mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// fl_vfs_read touches its file after it has scheduled the read. The read can run, hand the file back, and have the
// file's id closed in that gap, and the file must still be there. A test seam inside fl_vfs_read runs the read to
// completion and closes the file id at exactly that point. Under ASAN a missing reference on the file is a
// use-after-free.

static FlVfsHandle s_race_file;
static int s_race_closes_before;
static int s_race_closes_in_gap;

static void vfs_close_file_in_the_gap(FlVfsHandle read) {
    fl_vfs_wait(read); // main thread: the read runs to completion and hands the file back
    fl_vfs_close(s_race_file);
    s_race_closes_in_gap = s_stub_vfs_close_count - s_race_closes_before;
}

UTEST(vfs, closing_a_file_while_its_read_is_being_scheduled_leaves_the_read_intact) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    vfs_wait_all();

    s_race_file = fl_vfs_mount_open(mount.mount, FL_S(STUB_VFS_FILE), FlVfsOpenFlags_Read);
    fl_vfs_wait(s_race_file);
    u8 buffer[sizeof(k_stub_vfs_data)] = { 0 };

    s_race_closes_before = s_stub_vfs_close_count;
    s_race_closes_in_gap = -1;
    g_vfs_test_after_read_scheduled = vfs_close_file_in_the_gap;
    FlVfsHandle read = fl_vfs_read(s_race_file, buffer, sizeof(buffer));
    g_vfs_test_after_read_scheduled = nullptr;

    ASSERT_NE(read, FL_VFS_HANDLE_INVALID);
    ASSERT_EQ_MSG(s_race_closes_in_gap, 0, "fl_vfs_read still uses the file, so closing its id must not free it yet");
    ASSERT_EQ_MSG(s_stub_vfs_close_count - s_race_closes_before, 1, "the file goes once fl_vfs_read is done with it");
    ASSERT_TRUE(fl_vfs_get_data(read).success);
    ASSERT_EQ(memcmp(buffer, k_stub_vfs_data, sizeof(k_stub_vfs_data)), 0);

    fl_vfs_close(read);
    fl_vfs_mount_close(mount.mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The release hook is caller code and runs outside every VFS lock on each path that can run it: a close of a finished
// op, and the end of the job of an op closed while it was queued. A hook that calls back into the VFS would deadlock
// on the registry lock if it ever ran under it.

typedef struct VfsReentrantCtx {
    _Atomic FlVfsHandle other;
    _Atomic int calls;
} VfsReentrantCtx;

static void vfs_reentrant_release(void* user_data) {
    VfsReentrantCtx* ctx = (VfsReentrantCtx*)user_data;
    FlVfsHandle other = atomic_load(&ctx->other);
    (void)fl_vfs_is_ready(other);
    fl_vfs_close(other);
    atomic_fetch_add(&ctx->calls, 1);
}

UTEST(vfs, a_release_hook_may_call_back_into_the_vfs) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    vfs_wait_all();

    // On the closer's thread: a finished op released by its own close.
    static VfsReentrantCtx on_close;
    memset(&on_close, 0, sizeof(on_close));
    FlVfsHandle other_close
        = vfs_mount_read_all_prepare(mount.mount, FL_S(STUB_VFS_FILE), NULL, NULL, NULL, FL_VFS_HANDLE_INVALID);
    atomic_init(&on_close.other, other_close);
    FlVfsHandle finished = fl_vfs_mount_read_all_with_options(mount.mount, FL_S(STUB_VFS_FILE), NULL, &on_close,
                                                              vfs_reentrant_release, FL_VFS_HANDLE_INVALID);
    fl_vfs_wait(finished);
    fl_vfs_close(finished);
    ASSERT_EQ(atomic_load(&on_close.calls), 1);
    ASSERT_EQ_MSG(vfs_get_status(other_close), (VfsOpStatus)VFS_STATE_ERROR,
                  "the hook's own close must have taken effect");

    // On the worker: an op closed while queued, released as its job ends.
    VfsParkCtx park;
    FlVfsHandle parker;
    bool parked = vfs_park_worker(&park, mount.mount, &parker, FL_S(STUB_VFS_FILE));
    static VfsReentrantCtx on_worker;
    memset(&on_worker, 0, sizeof(on_worker));
    FlVfsHandle other_worker
        = vfs_mount_read_all_prepare(mount.mount, FL_S(STUB_VFS_FILE), NULL, NULL, NULL, FL_VFS_HANDLE_INVALID);
    atomic_init(&on_worker.other, other_worker);
    FlVfsHandle queued = fl_vfs_mount_read_all_with_options(mount.mount, FL_S(STUB_VFS_FILE), NULL, &on_worker,
                                                            vfs_reentrant_release, FL_VFS_HANDLE_INVALID);
    fl_vfs_close(queued);
    atomic_store(&park.release, true);
    fl_vfs_wait(parker);
    vfs_wait_all();

    ASSERT_TRUE_MSG(parked, "the parking callback never ran");
    ASSERT_EQ(atomic_load(&on_worker.calls), 1);
    ASSERT_EQ(vfs_get_status(other_worker), (VfsOpStatus)VFS_STATE_ERROR);

    fl_vfs_close(parker);
    fl_vfs_mount_close(mount.mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A fl_vfs_read runs against the caller's buffer, and its close is the caller's signal that the buffer may go, so
// that one close still waits for the job. A helper thread opens the gate after the close has begun; the close
// must not return before the read has filled the buffer.

typedef struct VfsDelayedReleaseCtx {
    VfsParkCtx* park;
    _Atomic bool released;
} VfsDelayedReleaseCtx;

static void* vfs_delayed_release_thread(void* user_data) {
    VfsDelayedReleaseCtx* ctx = (VfsDelayedReleaseCtx*)user_data;
    sleep_ms(50);
    atomic_store(&ctx->released, true);
    atomic_store(&ctx->park->release, true);
    return nullptr;
}

UTEST(vfs, close_of_a_queued_vfs_read_waits_for_the_buffer_it_lent) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    vfs_wait_all();
    ASSERT_TRUE(fl_vfs_mount_is_ready(mount.mount));

    VfsParkCtx park;
    FlVfsHandle parker;
    bool parked = vfs_park_worker(&park, mount.mount, &parker, FL_S(STUB_VFS_FILE));

    FlVfsHandle file = fl_vfs_mount_open(mount.mount, FL_S(STUB_VFS_FILE), FlVfsOpenFlags_Read); // queued behind parker
    u8 buf[sizeof(k_stub_vfs_data)] = { 0 };
    FlVfsHandle read = fl_vfs_read(file, buf, sizeof(buf));

    VfsDelayedReleaseCtx delayed = { .park = &park };
    atomic_init(&delayed.released, false);
    Thread releaser;
    bool thread_started = os_thread_create(&releaser, vfs_delayed_release_thread, &delayed);
    if (!thread_started) {
        atomic_store(&park.release, true);
    }

    fl_vfs_close(read); // the gate opens on the helper thread while this waits
    bool gate_was_open = atomic_load(&delayed.released);
    bool buffer_filled = memcmp(buf, k_stub_vfs_data, sizeof(k_stub_vfs_data)) == 0;

    if (thread_started) {
        os_thread_join(&releaser);
    }
    fl_vfs_wait(parker);

    ASSERT_TRUE_MSG(parked, "the parking callback never ran");
    ASSERT_TRUE_MSG(thread_started, "the releasing thread never started");
    ASSERT_NE(file, FL_VFS_HANDLE_INVALID);
    ASSERT_NE(read, FL_VFS_HANDLE_INVALID);
    ASSERT_TRUE_MSG(gate_was_open, "closing a fl_vfs_read must wait for the job the buffer is lent to");
    ASSERT_TRUE_MSG(buffer_filled, "the read must have filled the caller's buffer before its close returned");

    fl_vfs_close(file);
    fl_vfs_close(parker);
    fl_vfs_mount_close(mount.mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A mount resolves its source once and publishes one status. A source that does not resolve leaves the mount in
// the error state for good, and every operation against it fails cleanly instead of reaching for a root handle.

UTEST(vfs, mount_of_a_missing_source_reports_error_and_its_ops_fail) {
    fl_vfs_init(s_arena); // idempotent

    FlVfsMountResult mount = vfs_mount(FL_S("unit_test_vfs_no_such_source/none"));
    ASSERT_EQ(mount.status, FlVfsMountErrorStatus_Success);
    vfs_wait_all();

    ASSERT_EQ((int)fl_vfs_mount_get_status(mount.mount), (int)FlVfsMountStatus_Error);
    ASSERT_FALSE(fl_vfs_mount_is_ready(mount.mount));

    FlVfsHandle read
        = fl_vfs_mount_read_all_with_options(mount.mount, FL_S("x.bin"), NULL, NULL, NULL, FL_VFS_HANDLE_INVALID);
    ASSERT_NE(read, FL_VFS_HANDLE_INVALID);
    fl_vfs_wait(read);
    ASSERT_FALSE(fl_vfs_get_data(read).success);

    FlVfsDirHandle list = fl_vfs_get_listing(mount.mount, FL_S(""), 0);
    ASSERT_NE(list, FL_VFS_DIR_HANDLE_INVALID);
    fl_vfs_wait(list);
    ASSERT_FALSE(fl_vfs_get_file_list(list).success);

    fl_vfs_close(read);
    fl_vfs_close(list);
    fl_vfs_mount_close(mount.mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The mount job descends through the archives nested in the mount path and keeps exactly one handle: the root of
// the innermost archive. Every container it passed through is closed, and closing the mount closes the root.

UTEST(vfs, mount_resolves_an_archive_nested_in_its_path_and_keeps_one_root_handle) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_pfx_vfs_plugin);
    s_pfx_memory_mountable = true;

    const int allocs_before = s_pfx_alloc_count;
    const int closes_before = s_pfx_close_count;

    FlVfsMountResult mount = vfs_mount(FL_S("root/l0/l1"));
    ASSERT_EQ(mount.status, FlVfsMountErrorStatus_Success);
    vfs_wait_all();

    ASSERT_EQ((int)fl_vfs_mount_get_status(mount.mount), (int)FlVfsMountStatus_Ready);
    ASSERT_EQ_MSG(s_pfx_alloc_count - allocs_before, 5, "root + two probed files + two mounted archive layers");
    ASSERT_EQ_MSG((s_pfx_alloc_count - allocs_before) - (s_pfx_close_count - closes_before), 1,
                  "a ready mount holds only the innermost root handle");

    fl_vfs_mount_close(mount.mount);

    ASSERT_EQ_MSG(s_pfx_close_count - closes_before, s_pfx_alloc_count - allocs_before,
                  "closing the mount must close its root handle");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A listing asks the driver, sorts directories first and then in natural order, and flags the files an archive
// driver claims by name without opening any of them. LocalFS is the mount's driver here, so archives are
// recognised by this stand-in, which claims ".zip" names.

static bool arcname_supports_path(FlString path) {
    return fl_path_has_extension(path, FL_S("zip"));
}

static FlVfsOpenResult arcname_open(void* instance, void* parent_handle, FlString path, uint32_t flags,
                                    FlVfsMountOptions mount_options) {
    (void)instance;
    (void)parent_handle;
    (void)path;
    (void)flags;
    (void)mount_options;
    return (FlVfsOpenResult) { .handle = NULL, .error = stub_vfs_not_found() };
}

static void arcname_close(void* instance, void* handle) {
    (void)instance;
    (void)handle;
}

static FlVfsDriverCapabilities arcname_get_capabilities(void) {
    return (FlVfsDriverCapabilities)(FlVfsDriverCapabilities_Read | FlVfsDriverCapabilities_List
                                     | FlVfsDriverCapabilities_Archive);
}

static FlVfsPlugin s_arcname_vfs_plugin = {
    .api_version = FL_VFS_PLUGIN_API_VERSION,
    .plugin_name = FL_S_("ArcByName"),
    .supports_path = arcname_supports_path,
    .can_open = arcname_supports_path,
    .open = arcname_open,
    .close = arcname_close,
    .get_capabilities = arcname_get_capabilities,
};

static bool write_listing_fixture_file(FlString dir, FlString name) {
    arena_scratch_auto(temp);
    FlString path = file_build_path_os(temp.arena, dir, name);
    return file_write_os(path, k_stub_vfs_data, (i64)sizeof(k_stub_vfs_data)) == (i64)sizeof(k_stub_vfs_data);
}

UTEST(vfs, listing_puts_directories_first_in_natural_order_and_flags_archives_by_name) {
    FlArena* arena = arena_new();
    FlString cwd = get_current_directory_os(arena);
    FlString dir = file_build_path_os(arena, cwd, FL_S("unit_test_vfs_listing_tmp"));

    file_remove_directory_recursive_os(dir); // clean any leftover from an aborted run
    ASSERT_TRUE(file_create_directory_os(dir));
    ASSERT_TRUE(file_create_directory_os(file_build_path_os(arena, dir, FL_S("zdir"))));
    ASSERT_TRUE(file_create_directory_os(file_build_path_os(arena, dir, FL_S("adir"))));
    ASSERT_TRUE(write_listing_fixture_file(dir, FL_S("b10.txt")));
    ASSERT_TRUE(write_listing_fixture_file(dir, FL_S("b2.txt")));
    ASSERT_TRUE(write_listing_fixture_file(dir, FL_S("a.zip")));
    ASSERT_TRUE(write_listing_fixture_file(file_build_path_os(arena, dir, FL_S("adir")), FL_S("inner.txt")));

    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_arcname_vfs_plugin);

    FlVfsMountResult mount = vfs_mount(dir);
    ASSERT_EQ(mount.status, FlVfsMountErrorStatus_Success);
    vfs_wait_all();
    ASSERT_TRUE(fl_vfs_mount_is_ready(mount.mount));

    FlVfsDirHandle root = fl_vfs_get_listing(mount.mount, FL_S(""), 0);
    fl_vfs_wait(root);
    FlVfsFileList files = fl_vfs_get_file_list(root);
    ASSERT_TRUE(files.success);
    ASSERT_EQ((int)files.count, 5);

    const char* expected[] = { "adir", "zdir", "a.zip", "b2.txt", "b10.txt" };
    for (u32 i = 0; i < files.count; i++) {
        ASSERT_TRUE_MSG(fl_string_equals(files.entries[i].name, string_from_cstr(expected[i])),
                        "entries are directories first, then natural order");
        ASSERT_EQ(files.entries[i].is_directory, i < 2);
        ASSERT_EQ_MSG(files.entries[i].is_archive, i == 2, "only the name the archive driver claims is an archive");
    }

    FlVfsDirHandle sub = fl_vfs_get_listing(mount.mount, FL_S("adir"), 0);
    fl_vfs_wait(sub);
    FlVfsFileList inner = fl_vfs_get_file_list(sub);
    ASSERT_TRUE(inner.success);
    ASSERT_EQ((int)inner.count, 1);
    ASSERT_TRUE(fl_string_equals(inner.entries[0].name, FL_S("inner.txt")));

    // A directory that is not there lists as empty, as it did before mounts became immutable.
    FlVfsDirHandle missing = fl_vfs_get_listing(mount.mount, FL_S("no_such_dir"), 0);
    fl_vfs_wait(missing);
    FlVfsFileList none = fl_vfs_get_file_list(missing);
    ASSERT_TRUE(none.success);
    ASSERT_EQ((int)none.count, 0);

    fl_vfs_close(missing);
    fl_vfs_close(root);
    fl_vfs_close(sub);
    fl_vfs_mount_close(mount.mount);
    file_remove_directory_recursive_os(dir);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// One mutex per mount serializes the calls into its driver: operations from several workers on the same mount
// never overlap inside the driver. The driver sleeps in every call, so an overlap would be seen.

static _Atomic int s_serial_in_flight;
static _Atomic int s_serial_max_in_flight;
static _Atomic int s_serial_reads;
static _Atomic int s_serial_lists;

static void serial_enter(void) {
    int now = atomic_fetch_add(&s_serial_in_flight, 1) + 1;
    int seen = atomic_load(&s_serial_max_in_flight);
    while (now > seen && !atomic_compare_exchange_weak(&s_serial_max_in_flight, &seen, now)) {}
    sleep_ms(1);
}

static void serial_leave(void) {
    atomic_fetch_sub(&s_serial_in_flight, 1);
}

static FlVfsOpenResult serial_open(void* instance, void* parent_handle, FlString path, uint32_t flags,
                                   FlVfsMountOptions mount_options) {
    if (parent_handle == NULL) {
        if (!fl_string_equals(path, FL_S("serial"))) {
            return (FlVfsOpenResult) { .handle = NULL, .error = stub_vfs_not_found() };
        }
        StubVfsHandle* root = stub_vfs_alloc(true);
        return (FlVfsOpenResult) { .handle = root, .is_directory = true, .error = stub_vfs_ok() };
    }

    serial_enter();
    FlVfsOpenResult result = stub_vfs_open(instance, parent_handle, path, flags, mount_options);
    serial_leave();
    return result;
}

static FlVfsSizeResult serial_get_size(void* instance, void* handle, FlString relative_path) {
    serial_enter();
    FlVfsSizeResult result = stub_vfs_get_size(instance, handle, relative_path);
    serial_leave();
    return result;
}

static FlVfsReadResult serial_read(void* instance, void* handle, FlString relative_path, uint8_t* buffer,
                                   int64_t size) {
    serial_enter();
    FlVfsReadResult result = stub_vfs_read(instance, handle, relative_path, buffer, size);
    atomic_fetch_add(&s_serial_reads, 1);
    serial_leave();
    return result;
}

static void serial_list_directory(void* instance, void* handle, FlString relative_path, FlVfsListingCallback callback,
                                  void* user_data) {
    serial_enter();
    stub_vfs_list_directory(instance, handle, relative_path, callback, user_data);
    atomic_fetch_add(&s_serial_lists, 1);
    serial_leave();
}

static FlVfsPlugin s_serial_vfs_plugin = {
    .api_version = FL_VFS_PLUGIN_API_VERSION,
    .plugin_name = FL_S_("SerialFS"),
    .supports_path = stub_vfs_supports_path,
    .can_open = stub_vfs_can_open,
    .open = serial_open,
    .is_directory = stub_vfs_is_directory,
    .read = serial_read,
    .get_size = serial_get_size,
    .list_directory = serial_list_directory,
    .close = stub_vfs_close,
    .get_capabilities = stub_vfs_get_capabilities,
};

// The unit_tests job system has a single worker, so the operations are run by hand from several threads instead,
// the way separate workers would run them.
typedef struct SerialRunCtx {
    VfsOp* reads[8];
    int read_count;
    VfsOp* chained[4]; // hand-built ops: a listing and a read of an open file, run next to the read-alls
    int chained_count;
} SerialRunCtx;

static void* serial_run_thread(void* user_data) {
    SerialRunCtx* ctx = user_data;
    for (int i = 0; i < ctx->read_count; i++) {
        vfs_ops_execute_sync(ctx->reads[i]);
    }
    for (int i = 0; i < ctx->chained_count; i++) {
        vfs_ops_execute_sync(ctx->chained[i]);
    }
    return nullptr;
}

UTEST(vfs, operations_on_one_mount_never_overlap_inside_its_driver) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_serial_vfs_plugin);
    atomic_store(&s_serial_in_flight, 0);
    atomic_store(&s_serial_max_in_flight, 0);
    atomic_store(&s_serial_reads, 0);
    atomic_store(&s_serial_lists, 0);

    FlVfsMountResult mount = vfs_mount(FL_S("serial"));
    ASSERT_EQ(mount.status, FlVfsMountErrorStatus_Success);
    vfs_wait_all();
    ASSERT_TRUE(fl_vfs_mount_is_ready(mount.mount));

    FlVfsHandle file = fl_vfs_mount_open(mount.mount, FL_S(STUB_VFS_FILE), FlVfsOpenFlags_Read);
    fl_vfs_wait(file);

    enum { THREADS = 4, READS_PER_THREAD = 8 };
    FlVfsHandle reads[THREADS * READS_PER_THREAD];
    SerialRunCtx ctx[THREADS] = { 0 };
    VfsOp lists[THREADS] = { 0 };
    VfsOp file_reads[THREADS] = { 0 };
    u8 buffers[THREADS][sizeof(k_stub_vfs_data)];

    // Prepared but not dispatched: nothing runs until the threads below execute them.
    for (int i = 0; i < THREADS * READS_PER_THREAD; i++) {
        reads[i]
            = vfs_mount_read_all_prepare(mount.mount, FL_S(STUB_VFS_FILE), NULL, NULL, NULL, FL_VFS_HANDLE_INVALID);
        ASSERT_NE(reads[i], FL_VFS_HANDLE_INVALID);
        SerialRunCtx* owner = &ctx[i % THREADS];
        owner->reads[owner->read_count++] = vfs_op_acquire(reads[i]);
    }

    for (int i = 0; i < THREADS; i++) {
        lists[i].mount = mount.mount;
        lists[i].op_type = VfsOp_MountList;
        lists[i].path = FL_S("");
        file_reads[i].mount = mount.mount;
        file_reads[i].op_type = VfsOp_FileRead;
        file_reads[i].file_op = vfs_op_acquire(file); // a read owns its file reference and hands it back when run
        file_reads[i].read_buffer = buffers[i];
        file_reads[i].read_size = (i64)sizeof(buffers[i]);
        ctx[i].chained[ctx[i].chained_count++] = &lists[i];
        ctx[i].chained[ctx[i].chained_count++] = &file_reads[i];
    }

    Thread threads[THREADS];
    for (int i = 0; i < THREADS; i++) {
        ASSERT_TRUE(os_thread_create(&threads[i], serial_run_thread, &ctx[i]));
    }
    for (int i = 0; i < THREADS; i++) {
        os_thread_join(&threads[i]);
    }

    for (int i = 0; i < THREADS * READS_PER_THREAD; i++) {
        FlVfsData data = fl_vfs_get_data(reads[i]);
        ASSERT_TRUE(data.success);
        ASSERT_EQ((size_t)data.size, sizeof(k_stub_vfs_data));
        vfs_op_release(ctx[i % THREADS].reads[i / THREADS]); // the reference taken to run it by hand
        fl_vfs_close(reads[i]);
    }
    for (int i = 0; i < THREADS; i++) {
        ASSERT_TRUE(atomic_load(&lists[i].error_status) == VFS_STATE_SUCCESS);
        fl_arena_destroy(lists[i].result_arena);
        ASSERT_TRUE(atomic_load(&file_reads[i].error_status) == VFS_STATE_SUCCESS);
        mi_free(atomic_load(&file_reads[i].result_data));
    }

    // Every read-all and every hand-built file read went through the driver once.
    ASSERT_EQ(atomic_load(&s_serial_reads), THREADS * READS_PER_THREAD + THREADS);
    ASSERT_EQ(atomic_load(&s_serial_lists), THREADS);
    ASSERT_EQ_MSG(atomic_load(&s_serial_max_in_flight), 1, "two threads were inside the same mount's driver at once");

    fl_vfs_close(file);
    fl_vfs_mount_close(mount.mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Mounting from a worker thread runs the driver without holding any lock the rest of the VFS needs: while one
// thread is parked inside a driver's open, another mount starts and finishes.

static _Atomic bool s_gate_entered;
static _Atomic bool s_gate_release;

static FlVfsOpenResult gated_open(void* instance, void* parent_handle, FlString path, uint32_t flags,
                                  FlVfsMountOptions mount_options) {
    if (parent_handle != NULL) {
        return stub_vfs_open(instance, parent_handle, path, flags, mount_options);
    }
    if (!fl_string_equals(path, FL_S("gated"))) {
        return (FlVfsOpenResult) { .handle = NULL, .error = stub_vfs_not_found() };
    }

    atomic_store(&s_gate_entered, true);
    (void)vfs_await_flag(&s_gate_release);

    StubVfsHandle* root = stub_vfs_alloc(true);
    return (FlVfsOpenResult) { .handle = root, .is_directory = true, .error = stub_vfs_ok() };
}

static FlVfsPlugin s_gated_vfs_plugin = {
    .api_version = FL_VFS_PLUGIN_API_VERSION,
    .plugin_name = FL_S_("GatedFS"),
    .supports_path = stub_vfs_supports_path,
    .can_open = stub_vfs_can_open,
    .open = gated_open,
    .is_directory = stub_vfs_is_directory,
    .read = stub_vfs_read,
    .get_size = stub_vfs_get_size,
    .list_directory = stub_vfs_list_directory,
    .close = stub_vfs_close,
    .get_capabilities = stub_vfs_get_capabilities,
};

typedef struct GatedMountCtx {
    FlString source;
    FlVfsMount* mount;
    _Atomic bool done;
} GatedMountCtx;

// Runs as a job, so the mount below is resolved on the worker itself rather than scheduled.
static void gated_mount_job(void* user_data, FlJobsWorkerInfo info) {
    (void)info;
    GatedMountCtx* ctx = user_data;
    ctx->mount = vfs_mount(ctx->source).mount;
    atomic_store(&ctx->done, true);
}

static void* gated_other_mount_thread(void* user_data) {
    GatedMountCtx* ctx = user_data;
    ctx->mount = vfs_mount(ctx->source).mount;
    atomic_store(&ctx->done, true);
    return nullptr;
}

UTEST(vfs, mounting_from_a_worker_holds_no_global_lock_while_the_driver_runs) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);
    vfs_test_register_driver(&s_gated_vfs_plugin);
    atomic_store(&s_gate_entered, false);
    atomic_store(&s_gate_release, false);

    static GatedMountCtx gated;
    static GatedMountCtx other;
    memset(&gated, 0, sizeof(gated));
    memset(&other, 0, sizeof(other));
    gated.source = FL_S("gated");
    other.source = FL_S("/stub");
    atomic_init(&gated.done, false);
    atomic_init(&other.done, false);

    FlJobHandle gated_job = fl_jobs_add_job(gated_mount_job, &gated);
    for (int waited = 0; !atomic_load(&s_gate_entered) && waited < 5000; waited++) {
        sleep_ms(1);
    }
    const bool gate_entered = atomic_load(&s_gate_entered);
    if (!gate_entered) {
        atomic_store(&s_gate_release, true);
        fl_jobs_wait(gated_job);
    }
    ASSERT_TRUE_MSG(gate_entered, "the gated mount never reached its driver");

    Thread other_thread;
    const bool thread_created = os_thread_create(&other_thread, gated_other_mount_thread, &other);
    for (int waited = 0; thread_created && !atomic_load(&other.done) && waited < 3000; waited++) {
        sleep_ms(1);
    }
    bool other_started = atomic_load(&other.done);

    atomic_store(&s_gate_release, true);
    if (thread_created) {
        os_thread_join(&other_thread);
    }
    fl_jobs_wait(gated_job);
    ASSERT_TRUE(thread_created);

    ASSERT_TRUE_MSG(other_started, "a mount was blocked while another mount's driver call was in flight");

    vfs_wait_all();
    ASSERT_TRUE(fl_vfs_mount_is_ready(gated.mount));
    ASSERT_TRUE(fl_vfs_mount_is_ready(other.mount));
    fl_vfs_mount_close(gated.mount);
    fl_vfs_mount_close(other.mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A mount reports pending while its job is inside the driver, then ready once the job publishes.

UTEST(vfs, mount_reports_pending_until_its_job_publishes_ready) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_gated_vfs_plugin);
    atomic_store(&s_gate_entered, false);
    atomic_store(&s_gate_release, false);

    FlVfsMountResult mount = vfs_mount(FL_S("gated"));
    ASSERT_EQ(mount.status, FlVfsMountErrorStatus_Success);
    for (int waited = 0; !atomic_load(&s_gate_entered) && waited < 5000; waited++) {
        sleep_ms(1);
    }
    const bool entered = atomic_load(&s_gate_entered);
    const int status_while_gated = (int)fl_vfs_mount_get_status(mount.mount);

    atomic_store(&s_gate_release, true);
    vfs_wait_all();
    ASSERT_TRUE_MSG(entered, "the mount job never reached the driver");
    ASSERT_EQ(status_while_gated, (int)FlVfsMountStatus_Pending);
    ASSERT_EQ((int)fl_vfs_mount_get_status(mount.mount), (int)FlVfsMountStatus_Ready);

    fl_vfs_mount_close(mount.mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// fl_vfs_close never waits for the mount's driver. A worker parked inside a read holds the driver lock, and closing a
// different file of the same mount must still return at once.

typedef struct CloseDuringReadCtx {
    FlVfsHandle file;
    _Atomic bool done;
} CloseDuringReadCtx;

static void* close_during_read_thread(void* user_data) {
    CloseDuringReadCtx* ctx = user_data;
    fl_vfs_close(ctx->file);
    atomic_store(&ctx->done, true);
    return nullptr;
}

UTEST(vfs, closing_a_file_does_not_wait_for_a_read_in_flight_on_the_same_mount) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ(mount.status, FlVfsMountErrorStatus_Success);
    vfs_wait_all();

    FlVfsHandle busy = fl_vfs_mount_open(mount.mount, FL_S(STUB_VFS_FILE), FlVfsOpenFlags_Read);
    FlVfsHandle idle = fl_vfs_mount_open(mount.mount, FL_S(STUB_VFS_FILE), FlVfsOpenFlags_Read);
    fl_vfs_wait(busy);
    fl_vfs_wait(idle);

    u8 buffer[sizeof(k_stub_vfs_data)];
    atomic_store(&s_stub_vfs_read_entered, false);
    atomic_store(&s_stub_vfs_read_release, false);
    atomic_store(&s_stub_vfs_read_gate_armed, true);
    FlVfsHandle read = fl_vfs_read(busy, buffer, sizeof(buffer));
    for (int waited = 0; !atomic_load(&s_stub_vfs_read_entered) && waited < 5000; waited++) {
        sleep_ms(1);
    }
    bool parked = atomic_load(&s_stub_vfs_read_entered);

    CloseDuringReadCtx ctx = { .file = idle };
    atomic_init(&ctx.done, false);
    Thread closer;
    bool started = os_thread_create(&closer, close_during_read_thread, &ctx);
    for (int waited = 0; started && !atomic_load(&ctx.done) && waited < 3000; waited++) {
        sleep_ms(1);
    }
    bool closed_while_parked = atomic_load(&ctx.done);

    atomic_store(&s_stub_vfs_read_release, true);
    atomic_store(&s_stub_vfs_read_gate_armed, false);
    if (started) {
        os_thread_join(&closer);
    }
    fl_vfs_wait(read);

    ASSERT_TRUE_MSG(parked, "the read never reached the driver");
    ASSERT_TRUE(started);
    ASSERT_TRUE_MSG(closed_while_parked, "fl_vfs_close waited for a driver call in flight on its mount");

    fl_vfs_close(read);
    fl_vfs_close(busy);
    fl_vfs_mount_close(mount.mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Loading an archive onto a tree node. The stand-in archive driver takes ownership of the buffer open_memory is
// handed, as a real one does, and frees it when the handle closes.

typedef struct OvrHandle {
    uint8_t* mem; // VFS-allocated buffer this handle owns
} OvrHandle;

static int s_ovr_alloc_count;
static int s_ovr_close_count;
static int s_ovr_mem_free_count;

static OvrHandle* ovr_alloc(uint8_t* mem) {
    OvrHandle* h = (OvrHandle*)malloc(sizeof(OvrHandle));
    h->mem = mem;
    s_ovr_alloc_count++;
    return h;
}

static bool ovr_can_open(FlString path) {
    (void)path;
    return true;
}

static FlVfsReadResult ovr_read(void* instance, void* handle, FlString relative_path, uint8_t* buffer, int64_t size) {
    (void)instance;
    (void)handle;
    (void)relative_path;
    memset(buffer, 0xCD, (size_t)size);
    return (FlVfsReadResult) { .bytes_read = size, .error = stub_vfs_ok() };
}

static bool ovr_is_directory(void* instance, void* handle) {
    (void)instance;
    return handle != nullptr;
}

static const uint8_t* s_ovr_refused_data;

static FlVfsOpenResult ovr_open_memory_refusing(void* instance, const uint8_t* data, uint64_t size,
                                                FlString original_path) {
    (void)instance;
    s_ovr_refused_data = data;
    (void)size;
    (void)original_path;
    return (FlVfsOpenResult) { .handle = nullptr, .error = { .vfs_status = FlVfsStatus_ArchiveCorrupt } };
}

static void ovr_close(void* instance, void* handle) {
    (void)instance;
    OvrHandle* h = (OvrHandle*)handle;
    s_ovr_close_count++;
    if (h->mem) {
        mi_free(h->mem);
        s_ovr_mem_free_count++;
    }
    free(h);
}

static FlVfsDriverCapabilities ovr_get_capabilities(void) {
    return (FlVfsDriverCapabilities)(FlVfsDriverCapabilities_Read | FlVfsDriverCapabilities_List
                                     | FlVfsDriverCapabilities_Memory);
}

static FlVfsPlugin s_ovr_refusing_vfs_plugin = {
    .api_version = FL_VFS_PLUGIN_API_VERSION,
    .plugin_name = FL_S_("OvrRefusingArc"),
    .can_open = ovr_can_open,
    .read = ovr_read,
    .is_directory = ovr_is_directory,
    .close = ovr_close,
    .get_capabilities = ovr_get_capabilities,
    .open_memory = ovr_open_memory_refusing,
    .get_file_path = nullptr,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A driver that refuses the archive leaves the buffer the VFS read for it with the VFS, which frees it once.

#if USE_MIMALLOC
static int s_mi_error_count;

static void count_mi_error(int err, void* arg) {
    (void)err;
    (void)arg;
    s_mi_error_count++;
}
#endif

#if USE_MIMALLOC
typedef struct BlockProbe {
    const void* block;
    bool found;
} BlockProbe;

static bool probe_block(const mi_heap_t* heap, const mi_heap_area_t* area, void* block, size_t size, void* arg) {
    (void)heap;
    (void)area;
    (void)size;
    BlockProbe* probe = arg;
    probe->found |= block == probe->block;
    return true;
}
#endif

UTEST(vfs, archive_memory_load_refused_leaves_the_node_untouched_and_frees_the_buffer_once) {
    const int allocs_before = s_ovr_alloc_count;
    const int closes_before = s_ovr_close_count;
    const int frees_before = s_ovr_mem_free_count;

    FlArena* arena = arena_new();
    FlVfsMount mount = { .nodes_arena = arena };
    VfsPluginEntry entry = { .plugin = &s_ovr_refusing_vfs_plugin, .plugin_instance = nullptr };
    VfsState state = { .plugin_first = &entry, .plugin_last = &entry };
    VfsTreeNode* node = vfs_tree_create_node(&mount, nullptr, FL_S("archive.zip"));
    OvrHandle* parent_handle = ovr_alloc(nullptr);

#if USE_MIMALLOC
    const int mi_errors_before = s_mi_error_count;
    mi_register_error(count_mi_error, nullptr);
#endif
    bool ok = vfs_tree_try_load_as_archive(&state, node, &entry, parent_handle, FL_S("archive.zip"), 32, arena);
#if USE_MIMALLOC
    mi_register_error(nullptr, nullptr);
    ASSERT_EQ_MSG(s_mi_error_count - mi_errors_before, 0, "the buffer must not be freed twice");

    BlockProbe probe = { .block = s_ovr_refused_data };
    mi_heap_visit_blocks(mi_heap_get_default(), true, probe_block, &probe);
    ASSERT_FALSE_MSG(probe.found, "the buffer the driver refused must have been freed");
#endif

    ASSERT_FALSE(ok);
    ASSERT_TRUE(node->handle == nullptr);
    ASSERT_TRUE(node->plugin_entry == nullptr);
    ASSERT_FALSE(node->is_directory);
    ASSERT_EQ_MSG(s_ovr_mem_free_count - frees_before, 0, "a refused buffer is not the driver's to free");

    ovr_close(nullptr, parent_handle);
    vfs_tree_cleanup_node(node);
    fl_arena_destroy(arena);

    ASSERT_EQ(s_ovr_alloc_count - allocs_before, s_ovr_close_count - closes_before);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// With libarchive built in: real archives. The zips are written by hand (stored entries) so the cases need no
// fixture files. Archives are nested inside archives, and every path through any number of layers must list and read
// through a mount of the directory, of the outer archive, or of a nested path directly.

#if FLOWI_VFS_DRIVER_LIBARCHIVE

#define ZIP_ENTRY_COUNT 4

static const char* const k_zip_names[ZIP_ENTRY_COUNT] = { "file1.txt", "file2.txt", "file3.txt", "file4.txt" };
static const char* const k_zip_contents[ZIP_ENTRY_COUNT]
    = { "Content 1\n", "Content 2\n", "Content 3\n", "Content 4\n" };

static void build_stored_zip(ByteWriter* zip) {
    ZipEntry entries[ZIP_ENTRY_COUNT];
    for (int i = 0; i < ZIP_ENTRY_COUNT; i++) {
        entries[i] = (ZipEntry) { k_zip_names[i], (const u8*)k_zip_contents[i], strlen(k_zip_contents[i]) };
    }
    zip_build(zip, entries, ZIP_ENTRY_COUNT);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define STRESS_THREADS 4
#define STRESS_ITERATIONS 25

typedef struct ZipStressCtx {
    FlVfsMount* mount;
    _Atomic int failures;
} ZipStressCtx;

static void* zip_stress_thread(void* user_data) {
    ZipStressCtx* ctx = user_data;

    for (int iteration = 0; iteration < STRESS_ITERATIONS; iteration++) {
        FlVfsHandle handles[ZIP_ENTRY_COUNT];
        for (int i = 0; i < ZIP_ENTRY_COUNT; i++) {
            handles[i] = vfs_mount_read_all(ctx->mount, string_from_cstr(k_zip_names[i]));
        }

        for (int i = 0; i < ZIP_ENTRY_COUNT; i++) {
            fl_vfs_wait(handles[i]);
            FlVfsData data = fl_vfs_get_data(handles[i]);
            size_t expected_size = strlen(k_zip_contents[i]);
            if (!data.success || (size_t)data.size != expected_size
                || memcmp(data.data, k_zip_contents[i], expected_size) != 0) {
                atomic_fetch_add(&ctx->failures, 1);
            }
            fl_vfs_close(handles[i]);
        }
    }

    return nullptr;
}

UTEST(vfs_libarchive, concurrent_reads_through_one_archive_mount_return_the_right_bytes) {
    fl_vfs_init(s_arena); // idempotent
    fl_vfs_register_builtin_drivers();

    FlArena* arena = arena_new();
    FlString dir = vfs_test_temp_dir(arena, FL_S("unit_test_vfs_zip_tmp"));
    ASSERT_TRUE(dir.length > 0);
    FlString zip_path = file_build_path_os(arena, dir, FL_S("stress.zip"));

    ByteWriter* zip = byte_writer_create(arena);
    build_stored_zip(zip);
    ASSERT_EQ(file_write_os(zip_path, zip->bytes, (i64)zip->size), (i64)zip->size);

    FlVfsMount* archive = vfs_mount(zip_path).mount;
    ASSERT_TRUE(archive != NULL);
    vfs_wait_all();
    ASSERT_TRUE_MSG(fl_vfs_mount_is_ready(archive), "the archive must mount by its own path");

    ZipStressCtx ctx = { .mount = archive };
    atomic_init(&ctx.failures, 0);
    Thread threads[STRESS_THREADS];
    for (int i = 0; i < STRESS_THREADS; i++) {
        ASSERT_TRUE(os_thread_create(&threads[i], zip_stress_thread, &ctx));
    }
    for (int i = 0; i < STRESS_THREADS; i++) {
        os_thread_join(&threads[i]);
    }
    ASSERT_EQ(atomic_load(&ctx.failures), 0);

    // The directory holding the archive reads through it too, as a plain path.
    FlVfsMount* directory = vfs_mount(dir).mount;
    ASSERT_TRUE(directory != NULL);
    vfs_wait_all();
    FlVfsHandle through = vfs_mount_read_all(directory, FL_S("stress.zip/file1.txt"));
    fl_vfs_wait(through);
    FlVfsData through_data = fl_vfs_get_data(through);
    ASSERT_TRUE_MSG(through_data.success, "a directory mount resolves a path into an archive");
    ASSERT_EQ((size_t)through_data.size, strlen(k_zip_contents[0]));
    fl_vfs_close(through);

    FlVfsDirHandle listing = fl_vfs_get_listing(directory, FL_S(""), 0);
    fl_vfs_wait(listing);
    FlVfsFileList files = fl_vfs_get_file_list(listing);
    ASSERT_TRUE(files.success);
    ASSERT_EQ((int)files.count, 1);
    fl_vfs_close(listing);

    fl_vfs_mount_close(directory);
    fl_vfs_mount_close(archive);
    file_remove_directory_recursive_os(dir);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Nested archives. The fixture directory holds
//
//   outer.zip   top.txt, sub/side.zip (hello.txt), mid.zip
//   mid.zip     mid.txt, sub/deep.txt, inner.zip
//   inner.zip   hello.txt
//   bad.zip     ok.txt, broken.zip (not an archive)
//   folder/lone.zip   hello.txt
//   corrupt.zip   not an archive
//   plain.txt

#define NESTED_INNER_HELLO "inner hello\n"
#define NESTED_SIDE_HELLO "side hello\n"
#define NESTED_DEEP "deep\n"
#define NESTED_MID "mid\n"
#define NESTED_TOP "top\n"
#define NESTED_LONE_HELLO "lone hello\n"
#define NESTED_OK "ok\n"

typedef struct NestedFixture {
    FlArena* arena;
    FlString dir;
} NestedFixture;

static bool nested_write_file(NestedFixture* fixture, FlString relative_path, const ByteWriter* zip) {
    return vfs_test_write_file(fixture->arena, fixture->dir, relative_path, zip);
}

static bool nested_fixture_create(NestedFixture* fixture, FlString dir_name) {
    fixture->arena = arena_new();
    fixture->dir = vfs_test_temp_dir(fixture->arena, dir_name);
    if (fixture->dir.length == 0
        || !file_create_directory_os(file_build_path_os(fixture->arena, fixture->dir, FL_S("folder")))) {
        return false;
    }

    ByteWriter* inner = byte_writer_create(fixture->arena);
    zip_build(inner, (ZipEntry[]) { ZIP_TEXT("hello.txt", NESTED_INNER_HELLO) }, 1);

    ByteWriter* mid = byte_writer_create(fixture->arena);
    zip_build(mid,
              (ZipEntry[]) { ZIP_TEXT("mid.txt", NESTED_MID), ZIP_TEXT("sub/deep.txt", NESTED_DEEP),
                             ZIP_FILE("inner.zip", inner) },
              3);

    ByteWriter* side = byte_writer_create(fixture->arena);
    zip_build(side, (ZipEntry[]) { ZIP_TEXT("hello.txt", NESTED_SIDE_HELLO) }, 1);

    ByteWriter* outer = byte_writer_create(fixture->arena);
    zip_build(
        outer,
        (ZipEntry[]) { ZIP_TEXT("top.txt", NESTED_TOP), ZIP_FILE("sub/side.zip", side), ZIP_FILE("mid.zip", mid) }, 3);

    ByteWriter* lone = byte_writer_create(fixture->arena);
    zip_build(lone, (ZipEntry[]) { ZIP_TEXT("hello.txt", NESTED_LONE_HELLO) }, 1);

    ByteWriter* bad = byte_writer_create(fixture->arena);
    zip_build(bad,
              (ZipEntry[]) { ZIP_TEXT("ok.txt", NESTED_OK),
                             ZIP_TEXT("broken.zip", "PK this is not an archive, only named like one") },
              2);

    FlString plain = file_build_path_os(fixture->arena, fixture->dir, FL_S("plain.txt"));
    FlString corrupt = file_build_path_os(fixture->arena, fixture->dir, FL_S("corrupt.zip"));
    return file_write_os(corrupt, (const u8*)"PK this file is named like an archive", 37) == 37
           && nested_write_file(fixture, FL_S("outer.zip"), outer)
           && nested_write_file(fixture, FL_S("folder/lone.zip"), lone)
           && nested_write_file(fixture, FL_S("bad.zip"), bad) && file_write_os(plain, (const u8*)"plain\n", 6) == 6;
}

static void nested_fixture_destroy(NestedFixture* fixture) {
    file_remove_directory_recursive_os(fixture->dir);
    fl_arena_destroy(fixture->arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Paths are prefix + name, where prefix is empty or ends in '/'.
static FlString nested_path(FlString prefix, const char* name) {
    return fl_sprintf_arena(s_arena, "%S%s", prefix, name);
}

static bool nested_expect_read(FlVfsMount* mount, FlString prefix, const char* name, const char* expected) {
    return vfs_expect_read(mount, nested_path(prefix, name), string_from_cstr(expected));
}

// The bytes of an archive file read as a file, which the tree must still serve after it has entered the archive.
static bool nested_expect_raw_archive(FlVfsMount* mount, const char* path) {
    FlVfsHandle handle = vfs_mount_read_all(mount, string_from_cstr(path));
    fl_vfs_wait(handle);
    FlVfsData data = fl_vfs_get_data(handle);
    bool ok = data.success && data.size > 4 && memcmp(data.data, "PK\x03\x04", 4) == 0;
    if (!ok) {
        printf("raw read of '%s' failed (success=%d size=%lld)\n", path, data.success, (long long)data.size);
    }
    fl_vfs_close(handle);
    return ok;
}

static bool nested_expect_read_fails(FlVfsMount* mount, FlString prefix, const char* name) {
    return vfs_expect_read_fails(mount, nested_path(prefix, name));
}

// name is the directory inside prefix ("" for prefix itself).
static bool nested_expect_list(FlVfsMount* mount, FlString prefix, const char* name, const char* expected) {
    FlString path = nested_path(prefix, name);
    if (path.length > 0 && path.data[path.length - 1] == '/') {
        path.length--;
    }
    return vfs_expect_list(mount, path, expected);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Each level checks its own contents and then the level below it, so one call covers a whole chain. prefix is where
// the archive's root sits in the mount.

static bool nested_check_inner(FlVfsMount* mount, FlString prefix) {
    return nested_expect_list(mount, prefix, "", "hello.txt")
           && nested_expect_read(mount, prefix, "hello.txt", NESTED_INNER_HELLO);
}

static bool nested_check_mid(FlVfsMount* mount, FlString prefix) {
    return nested_expect_list(mount, prefix, "", "sub/ inner.zip* mid.txt")
           && nested_expect_list(mount, prefix, "sub", "deep.txt")
           && nested_expect_read(mount, prefix, "mid.txt", NESTED_MID)
           && nested_expect_read(mount, prefix, "sub/deep.txt", NESTED_DEEP)
           && nested_check_inner(mount, nested_path(prefix, "inner.zip/"));
}

static bool nested_check_outer(FlVfsMount* mount, FlString prefix) {
    return nested_expect_list(mount, prefix, "", "sub/ mid.zip* top.txt")
           && nested_expect_list(mount, prefix, "sub", "side.zip*")
           && nested_expect_read(mount, prefix, "top.txt", NESTED_TOP)
           && nested_expect_list(mount, prefix, "sub/side.zip", "hello.txt")
           && nested_expect_read(mount, prefix, "sub/side.zip/hello.txt", NESTED_SIDE_HELLO)
           && nested_check_mid(mount, nested_path(prefix, "mid.zip/"))
           && nested_expect_read_fails(mount, prefix, "missing.txt")
           && nested_expect_read_fails(mount, prefix, "mid.zip/missing.txt")
           && nested_expect_read_fails(mount, prefix, "mid.zip/inner.zip/missing.txt")
           && nested_expect_read_fails(mount, prefix, "top.txt/nothing");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool nested_begin(NestedFixture* fixture, FlString dir_name) {
    return vfs_test_begin() && nested_fixture_create(fixture, dir_name);
}

UTEST(vfs_libarchive, nested_archives_list_and_read_through_a_directory_mount) {
    NestedFixture fixture;
    ASSERT_TRUE(nested_begin(&fixture, FL_S("unit_test_vfs_nested_dir_tmp")));

    FlVfsMount* mount = vfs_test_mount(fixture.dir);
    ASSERT_TRUE(mount != nullptr);

    ASSERT_TRUE(nested_expect_list(mount, FL_S(""), "", "folder/ bad.zip* corrupt.zip* outer.zip* plain.txt"));
    ASSERT_TRUE(nested_expect_list(mount, FL_S(""), "folder", "lone.zip*"));
    ASSERT_TRUE(nested_expect_read(mount, FL_S(""), "folder/lone.zip/hello.txt", NESTED_LONE_HELLO));
    ASSERT_TRUE(nested_check_outer(mount, FL_S("outer.zip/")));
    ASSERT_TRUE(nested_expect_raw_archive(mount, "outer.zip"));
    ASSERT_TRUE(nested_expect_raw_archive(mount, "outer.zip/mid.zip"));
    ASSERT_TRUE(nested_expect_raw_archive(mount, "outer.zip/mid.zip/inner.zip"));
    ASSERT_TRUE(nested_expect_raw_archive(mount, "outer.zip/sub/side.zip"));

    // A caller may also name the file by its absolute path, which the local driver accepts below any directory mount.
    FlString absolute = fl_sprintf_arena(s_arena, "%S/", fixture.dir);
    ASSERT_TRUE(nested_expect_read(mount, absolute, "plain.txt", "plain\n"));
    ASSERT_TRUE(nested_expect_read(mount, absolute, "outer.zip/mid.zip/mid.txt", NESTED_MID));
    ASSERT_TRUE(nested_expect_list(mount, absolute, "outer.zip/mid.zip", "sub/ inner.zip* mid.txt"));

    fl_vfs_mount_close(mount);
    vfs_destroy();
    nested_fixture_destroy(&fixture);
}

UTEST(vfs_libarchive, nested_archives_list_and_read_through_a_mount_of_the_outer_archive) {
    NestedFixture fixture;
    ASSERT_TRUE(nested_begin(&fixture, FL_S("unit_test_vfs_nested_outer_tmp")));

    FlVfsMount* mount = vfs_test_mount(file_build_path_os(fixture.arena, fixture.dir, FL_S("outer.zip")));
    ASSERT_TRUE(mount != nullptr);
    ASSERT_TRUE(nested_check_outer(mount, FL_S("")));

    fl_vfs_mount_close(mount);
    vfs_destroy();
    nested_fixture_destroy(&fixture);
}

UTEST(vfs_libarchive, a_nested_path_mounts_directly_and_resolves_further_layers_below_it) {
    NestedFixture fixture;
    ASSERT_TRUE(nested_begin(&fixture, FL_S("unit_test_vfs_nested_direct_tmp")));

    FlString outer = file_build_path_os(fixture.arena, fixture.dir, FL_S("outer.zip"));

    FlVfsMount* mid = vfs_test_mount(fl_sprintf_arena(fixture.arena, "%S/mid.zip", outer));
    ASSERT_TRUE_MSG(mid != nullptr, "outer.zip/mid.zip must mount");
    ASSERT_TRUE(nested_check_mid(mid, FL_S("")));

    FlVfsMount* inner = vfs_test_mount(fl_sprintf_arena(fixture.arena, "%S/mid.zip/inner.zip", outer));
    ASSERT_TRUE_MSG(inner != nullptr, "outer.zip/mid.zip/inner.zip must mount");
    ASSERT_TRUE(nested_check_inner(inner, FL_S("")));

    FlVfsMount* side = vfs_test_mount(fl_sprintf_arena(fixture.arena, "%S/sub/side.zip", outer));
    ASSERT_TRUE_MSG(side != nullptr, "an archive inside a subdirectory of an archive must mount");
    ASSERT_TRUE(nested_expect_list(side, FL_S(""), "", "hello.txt"));
    ASSERT_TRUE(nested_expect_read(side, FL_S(""), "hello.txt", NESTED_SIDE_HELLO));

    fl_vfs_mount_close(mid);
    fl_vfs_mount_close(inner);
    fl_vfs_mount_close(side);
    vfs_destroy();
    nested_fixture_destroy(&fixture);
}

UTEST(vfs_libarchive, a_nested_file_opens_and_reads_through_a_chained_read) {
    NestedFixture fixture;
    ASSERT_TRUE(nested_begin(&fixture, FL_S("unit_test_vfs_nested_open_tmp")));

    FlVfsMount* mount = vfs_test_mount(fixture.dir);
    ASSERT_TRUE(mount != nullptr);

    FlVfsHandle file = fl_vfs_mount_open(mount, FL_S("outer.zip/mid.zip/inner.zip/hello.txt"), FlVfsOpenFlags_Read);
    ASSERT_NE(file, FL_VFS_HANDLE_INVALID);
    u8 buffer[64] = { 0 };
    FlVfsHandle read = fl_vfs_read(file, buffer, sizeof(buffer));
    ASSERT_NE(read, FL_VFS_HANDLE_INVALID);
    fl_vfs_wait(read);

    FlVfsData data = fl_vfs_get_data(read);
    ASSERT_TRUE(data.success);
    ASSERT_EQ((size_t)data.size, strlen(NESTED_INNER_HELLO));
    ASSERT_EQ(memcmp(buffer, NESTED_INNER_HELLO, strlen(NESTED_INNER_HELLO)), 0);

    fl_vfs_close(read);
    fl_vfs_close(file);
    fl_vfs_mount_close(mount);
    vfs_destroy();
    nested_fixture_destroy(&fixture);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Threads reading and listing at different depths through one mount, while the archives are opened for the first time.

#define NESTED_THREADS 6
#define NESTED_PATHS 5

static const char* const k_nested_paths[NESTED_PATHS] = {
    "outer.zip/top.txt",
    "outer.zip/sub/side.zip/hello.txt",
    "outer.zip/mid.zip/mid.txt",
    "outer.zip/mid.zip/sub/deep.txt",
    "outer.zip/mid.zip/inner.zip/hello.txt",
};
static const char* const k_nested_contents[NESTED_PATHS]
    = { NESTED_TOP, NESTED_SIDE_HELLO, NESTED_MID, NESTED_DEEP, NESTED_INNER_HELLO };

#define NESTED_LISTINGS 3

static const char* const k_nested_listings[NESTED_LISTINGS]
    = { "outer.zip/mid.zip", "outer.zip/mid.zip/inner.zip", "outer.zip/sub/side.zip" };
static const int k_nested_listing_counts[NESTED_LISTINGS] = { 3, 1, 1 };

// The unit_tests job system has one worker, so jobs never overlap. The ops are built by hand and run from several
// threads at once, as operations_on_one_mount_never_overlap_inside_its_driver does, with a fresh mount per round so
// that the first opens of every archive race inside the tree.

typedef struct NestedRaceCtx {
    VfsOp* reads[NESTED_PATHS];
    FlVfsHandle handles[NESTED_PATHS];
    int order[NESTED_PATHS];
    VfsOp lists[NESTED_LISTINGS];
} NestedRaceCtx;

static void* nested_race_thread(void* user_data) {
    NestedRaceCtx* ctx = user_data;
    for (int i = 0; i < NESTED_PATHS; i++) {
        vfs_ops_execute_sync(ctx->reads[i]);
        if (i < NESTED_LISTINGS) {
            vfs_ops_execute_sync(&ctx->lists[i]);
        }
    }
    return nullptr;
}

UTEST(vfs_libarchive, first_opens_of_nested_archives_race_inside_the_tree) {
    NestedFixture fixture;
    ASSERT_TRUE(nested_begin(&fixture, FL_S("unit_test_vfs_nested_race_tmp")));
    int failures = 0;

    for (int round = 0; round < 4; round++) {
        FlVfsMount* mount = vfs_test_mount(fixture.dir);
        ASSERT_TRUE(mount != nullptr);
        NestedRaceCtx ctx[NESTED_THREADS];
        memset(ctx, 0, sizeof(ctx));
        for (int t = 0; t < NESTED_THREADS; t++) {
            for (int i = 0; i < NESTED_PATHS; i++) {
                int p = (t + i) % NESTED_PATHS;
                ctx[t].order[i] = p;
                ctx[t].handles[i] = vfs_mount_read_all_prepare(mount, string_from_cstr(k_nested_paths[p]), nullptr,
                                                               nullptr, nullptr, FL_VFS_HANDLE_INVALID);
                ctx[t].reads[i] = vfs_op_acquire(ctx[t].handles[i]);
            }
            for (int i = 0; i < NESTED_LISTINGS; i++) {
                int l = (t + i) % NESTED_LISTINGS;
                ctx[t].lists[i].mount = mount;
                ctx[t].lists[i].op_type = VfsOp_MountList;
                ctx[t].lists[i].path = string_from_cstr(k_nested_listings[l]);
            }
        }
        Thread threads[NESTED_THREADS];
        for (int t = 0; t < NESTED_THREADS; t++) {
            ASSERT_TRUE(os_thread_create(&threads[t], nested_race_thread, &ctx[t]));
        }
        for (int t = 0; t < NESTED_THREADS; t++) {
            os_thread_join(&threads[t]);
        }
        for (int t = 0; t < NESTED_THREADS; t++) {
            for (int i = 0; i < NESTED_PATHS; i++) {
                FlVfsData data = fl_vfs_get_data(ctx[t].handles[i]);
                const char* expected = k_nested_contents[ctx[t].order[i]];
                if (!data.success || (size_t)data.size != strlen(expected)
                    || memcmp(data.data, expected, data.size) != 0) {
                    failures++;
                }
                vfs_op_release(ctx[t].reads[i]);
                fl_vfs_close(ctx[t].handles[i]);
            }
            for (int i = 0; i < NESTED_LISTINGS; i++) {
                if (atomic_load(&ctx[t].lists[i].error_status) != VFS_STATE_SUCCESS) {
                    failures++;
                }
                fl_arena_destroy(ctx[t].lists[i].result_arena);
            }
        }
        fl_vfs_mount_close(mount);
    }
    ASSERT_EQ(failures, 0);
    vfs_destroy();
    nested_fixture_destroy(&fixture);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// libarchive wrapped so the test can count the handles it hands out and takes back.

static FlVfsPlugin s_counting_archive_plugin;
static _Atomic int s_archive_opened;
static _Atomic int s_archive_closed;
static _Atomic int s_archive_from_file;   // archives opened in place from a file on disk
static _Atomic int s_archive_from_memory; // archives opened from a copy in memory

static FlVfsOpenResult counting_archive_open(void* instance, void* parent, FlString path, uint32_t flags,
                                             FlVfsMountOptions options) {
    FlVfsOpenResult result = vfs_driver_libarchive()->open(instance, parent, path, flags, options);
    if (result.handle) {
        atomic_fetch_add(&s_archive_opened, 1);
        if (!parent) {
            atomic_fetch_add(&s_archive_from_file, 1);
        }
    }
    return result;
}

static FlVfsOpenResult counting_archive_open_memory(void* instance, const uint8_t* data, uint64_t size,
                                                    FlString original_path) {
    FlVfsOpenResult result = vfs_driver_libarchive()->open_memory(instance, data, size, original_path);
    if (result.handle) {
        atomic_fetch_add(&s_archive_opened, 1);
        atomic_fetch_add(&s_archive_from_memory, 1);
    }
    return result;
}

static void counting_archive_close(void* instance, void* handle) {
    atomic_fetch_add(&s_archive_closed, 1);
    vfs_driver_libarchive()->close(instance, handle);
}

// A fresh VFS whose libarchive is the counting wrapper below, and the fixture directory.
static bool nested_begin_counting(NestedFixture* fixture, FlString dir_name) {
    process_singletons_reset();
    if (!fl_vfs_init(s_arena) || !nested_fixture_create(fixture, dir_name)) {
        return false;
    }

    s_counting_archive_plugin = *vfs_driver_libarchive();
    s_counting_archive_plugin.open = counting_archive_open;
    s_counting_archive_plugin.open_memory = counting_archive_open_memory;
    s_counting_archive_plugin.close = counting_archive_close;
    vfs_test_register_driver(&s_counting_archive_plugin);
    atomic_store(&s_archive_opened, 0);
    atomic_store(&s_archive_closed, 0);
    atomic_store(&s_archive_from_file, 0);
    atomic_store(&s_archive_from_memory, 0);
    return true;
}

UTEST(vfs_libarchive, a_corrupt_inner_archive_fails_cleanly_and_leaks_no_handle) {
    NestedFixture fixture;
    ASSERT_TRUE(nested_begin_counting(&fixture, FL_S("unit_test_vfs_nested_corrupt_tmp")));

    FlVfsMount* mount = vfs_test_mount(fixture.dir);
    ASSERT_TRUE(mount != nullptr);

    ASSERT_TRUE(nested_expect_read(mount, FL_S("bad.zip/"), "ok.txt", NESTED_OK));
    for (int attempt = 0; attempt < 3; attempt++) {
        ASSERT_TRUE(nested_expect_read_fails(mount, FL_S("bad.zip/"), "broken.zip/x.txt"));
    }

    FlVfsDirHandle listing = fl_vfs_get_listing(mount, FL_S("bad.zip/broken.zip"), 0);
    fl_vfs_wait(listing);
    FlVfsFileList files = fl_vfs_get_file_list(listing);
    ASSERT_TRUE(files.success);
    ASSERT_EQ_MSG((int)files.count, 0, "a corrupt archive is not entered, so nothing is listed in it");
    fl_vfs_close(listing);

    // A corrupt archive on disk is tried in place, then from memory, and fails both ways without a handle.
    const int opened_before_corrupt_file = atomic_load(&s_archive_opened);
    ASSERT_TRUE(nested_expect_read_fails(mount, FL_S("corrupt.zip/"), "x.txt"));
    ASSERT_EQ(atomic_load(&s_archive_opened), opened_before_corrupt_file);

    ASSERT_TRUE(nested_expect_read(mount, FL_S("outer.zip/mid.zip/"), "mid.txt", NESTED_MID));
    ASSERT_GT(atomic_load(&s_archive_opened), 0);

    fl_vfs_mount_close(mount);
    ASSERT_EQ_MSG(atomic_load(&s_archive_closed), atomic_load(&s_archive_opened),
                  "closing the mount must close every archive layer it opened");

    vfs_destroy();
    nested_fixture_destroy(&fixture);
}

typedef struct NestedCloseCtx {
    _Atomic int good;
    _Atomic int calls;
} NestedCloseCtx;

static FlVfsData nested_close_callback(void* data, i64 size, void* user_data) {
    NestedCloseCtx* ctx = user_data;
    atomic_fetch_add(&ctx->calls, 1);
    if (data && (size_t)size == strlen(NESTED_INNER_HELLO) && memcmp(data, NESTED_INNER_HELLO, size) == 0) {
        atomic_fetch_add(&ctx->good, 1);
    }
    return (FlVfsData) { .data = data, .size = size, .success = data != nullptr };
}

UTEST(vfs_libarchive, closing_a_mount_with_reads_in_flight_through_archives_finishes_them_and_closes_every_archive) {
    NestedFixture fixture;
    ASSERT_TRUE(nested_begin_counting(&fixture, FL_S("unit_test_vfs_nested_close_tmp")));

    FlVfsMount* mount = vfs_test_mount(fixture.dir);
    ASSERT_TRUE(mount != nullptr);

    static NestedCloseCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    for (int i = 0; i < 8; i++) {
        FlVfsHandle read = vfs_mount_read_all(mount, FL_S("outer.zip/mid.zip/inner.zip/hello.txt"),
                                              .callback = nested_close_callback, .user_data = &ctx);
        ASSERT_NE(read, FL_VFS_HANDLE_INVALID);
    }
    FlVfsHandle file = fl_vfs_mount_open(mount, FL_S("outer.zip/sub/side.zip/hello.txt"), FlVfsOpenFlags_Read);
    ASSERT_NE(file, FL_VFS_HANDLE_INVALID);

    fl_vfs_mount_close(mount);

    ASSERT_EQ_MSG(atomic_load(&ctx.calls), 8, "every queued read ran before the mount went");
    ASSERT_EQ_MSG(atomic_load(&ctx.good), 8, "and read the right bytes through the archives");
    ASSERT_GT(atomic_load(&s_archive_opened), 0);
    ASSERT_EQ(atomic_load(&s_archive_closed), atomic_load(&s_archive_opened));

    vfs_destroy();
    nested_fixture_destroy(&fixture);
}

UTEST(vfs_libarchive, a_nested_archive_is_opened_once_and_kept_for_the_mount) {
    NestedFixture fixture;
    ASSERT_TRUE(nested_begin_counting(&fixture, FL_S("unit_test_vfs_nested_once_tmp")));

    FlVfsMount* mount = vfs_test_mount(fixture.dir);
    ASSERT_TRUE(mount != nullptr);

    ASSERT_TRUE(nested_expect_read(mount, FL_S("outer.zip/mid.zip/inner.zip/"), "hello.txt", NESTED_INNER_HELLO));
    const int opened_by_first_read = atomic_load(&s_archive_opened);
    ASSERT_EQ(opened_by_first_read, 4); // three archives, plus the file handle the read opens inside the last
    ASSERT_EQ_MSG(atomic_load(&s_archive_from_file), 1, "outer.zip is opened in place from its file on disk");
    ASSERT_EQ_MSG(atomic_load(&s_archive_from_memory), 2, "mid.zip and inner.zip are opened from memory");

    ASSERT_TRUE(nested_expect_read(mount, FL_S("outer.zip/mid.zip/inner.zip/"), "hello.txt", NESTED_INNER_HELLO));
    ASSERT_TRUE(nested_expect_list(mount, FL_S("outer.zip/mid.zip/"), "inner.zip", "hello.txt"));
    ASSERT_EQ_MSG(atomic_load(&s_archive_opened) - opened_by_first_read, 1,
                  "the archives on the path stay open: only the file handle of the second read is new");

    fl_vfs_mount_close(mount);
    ASSERT_EQ(atomic_load(&s_archive_closed), atomic_load(&s_archive_opened));

    vfs_destroy();
    nested_fixture_destroy(&fixture);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// An LHA archive (stored, one file), alone on disk and inside a zip: archives of different drivers nest.

#if FLOWI_VFS_DRIVER_LHASA

UTEST(vfs_lhasa, an_lha_archive_nests_in_a_zip_and_sits_on_disk) {
    NestedFixture fixture;
    ASSERT_TRUE(nested_begin(&fixture, FL_S("unit_test_vfs_nested_lha_tmp")));

    ByteWriter* lha = byte_writer_create(fixture.arena);
    lha_build(lha, "song.txt", "lha song\n");
    ByteWriter* wrapper = byte_writer_create(fixture.arena);
    zip_build(wrapper, (ZipEntry[]) { ZIP_FILE("pack.lha", lha) }, 1);
    ASSERT_TRUE(nested_write_file(&fixture, FL_S("wrapper.zip"), wrapper));
    ASSERT_TRUE(nested_write_file(&fixture, FL_S("disk.lha"), lha));

    FlVfsMount* mount = vfs_test_mount(fixture.dir);
    ASSERT_TRUE(mount != nullptr);

    ASSERT_TRUE(nested_expect_list(mount, FL_S(""), "wrapper.zip", "pack.lha*"));
    ASSERT_TRUE(nested_expect_list(mount, FL_S(""), "wrapper.zip/pack.lha", "song.txt"));
    ASSERT_TRUE(nested_expect_read(mount, FL_S(""), "wrapper.zip/pack.lha/song.txt", "lha song\n"));
    ASSERT_TRUE(nested_expect_list(mount, FL_S(""), "disk.lha", "song.txt"));
    ASSERT_TRUE(nested_expect_read(mount, FL_S(""), "disk.lha/song.txt", "lha song\n"));

    fl_vfs_mount_close(mount);
    vfs_destroy();
    nested_fixture_destroy(&fixture);
}

#endif // FLOWI_VFS_DRIVER_LHASA

#endif // FLOWI_VFS_DRIVER_LIBARCHIVE
