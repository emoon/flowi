// VFS resolver tests: walking a path from a root handle through files, archives and plugin handles

#include "vfs_test_support.h"

static FlVfsDriverCapabilities memory_only_capabilities(void) {
    return FlVfsDriverCapabilities_Memory;
}

static FlVfsOpenResult memory_only_open_memory(void* instance, const uint8_t* data, uint64_t size, FlString path) {
    (void)instance;
    (void)data;
    (void)size;
    (void)path;
    return (FlVfsOpenResult) { 0 };
}

// A driver that accepts archives from memory and nothing else.
static FlVfsPlugin s_memory_only_plugin = {
    .api_version = FL_VFS_PLUGIN_API_VERSION,
    .plugin_name = FL_S_("MemoryOnly"),
    .get_capabilities = memory_only_capabilities,
    .open_memory = memory_only_open_memory,
};

static VfsState memory_only_state(VfsPluginEntry* entry) {
    *entry = (VfsPluginEntry) { .plugin = &s_memory_only_plugin };
    return (VfsState) { .plugin_first = entry, .plugin_last = entry };
}

// The root directory handle the resolver owns while walking the path, as for a mounted root.
static FlVfsOpenResult open_stub_root(void) {
    return stub_vfs_open(nullptr, nullptr, FL_S("/"), 0, (FlVfsMountOptions) { 0 });
}

static FinalDriver resolve_stub_file_from(VfsState* state, FlVfsOpenResult root) {
    static VfsPluginEntry root_entry = { .plugin = &s_stub_vfs_plugin, .plugin_instance = nullptr };
    PathComponents comp = path_components(s_arena, FL_S(STUB_VFS_FILE));
    return vfs_resolve_from_root(state, (DriverHandle) { .plugin_entry = &root_entry, .handle = root.handle }, &comp,
                                 s_arena);
}

// A short read (fewer bytes than get_size promised) takes the resolver's error branch, which closes the file
// handle try_open_file just opened before returning.
UTEST(vfs, resolve_short_read_closes_plugin_handle) {
    const LeakWatch leaks = leak_watch_begin(&s_stub_vfs_alloc_count, &s_stub_vfs_close_count);

    s_stub_vfs_open_as_file = true;
    s_stub_vfs_short_read = true;

    VfsPluginEntry memory_entry;
    VfsState state = memory_only_state(&memory_entry);

    FlVfsOpenResult root = open_stub_root();
    ASSERT_TRUE(root.handle != nullptr);
    ASSERT_TRUE(root.is_directory);

    FinalDriver result = resolve_stub_file_from(&state, root);

    s_stub_vfs_open_as_file = false;
    s_stub_vfs_short_read = false;

    ASSERT_EQ_MSG(result.error, VfsResolveError_InvalidFileSize, "short read must surface as InvalidFileSize");

    ASSERT_GT_MSG(leak_watch_allocated(&leaks), 1, "resolver should have opened the root and the probed file");
    ASSERT_EQ_MSG(leak_watch_closed(&leaks), leak_watch_allocated(&leaks),
                  "short-read error path must close every plugin handle it opened");
}

// Descending through nested archives (a zip inside a zip) moves to a new memory-mounted container at each layer
// and closes the container it leaves. This stub opens files (can_open/open) and archives from memory
// (open_memory): a probed "file" reads a few bytes, and open_memory mounts those bytes as a directory-shaped
// archive.

typedef struct NestHandle {
    bool is_dir;
    int serial; // 1-based allocation order; identifies a handle across a free that recycles its address
} NestHandle;

static _Atomic int s_nest_alloc_count;
static _Atomic int s_nest_close_count;

#define NEST_ARCHIVE_SIZE 8

static NestHandle* nest_alloc(bool is_dir) {
    NestHandle* h = (NestHandle*)malloc(sizeof(NestHandle));
    if (h) {
        h->is_dir = is_dir;
        h->serial = ++s_nest_alloc_count;
    }
    return h;
}

static bool nest_can_open(FlString path) {
    return vfs_segment_is(path, "l0") || vfs_segment_is(path, "l1");
}

static FlVfsOpenResult nest_open(void* instance, void* parent_handle, FlString path, uint32_t flags,
                                 FlVfsMountOptions mount_options) {
    (void)instance;
    (void)parent_handle;
    (void)flags;
    (void)mount_options;
    if (nest_can_open(path)) {
        NestHandle* h = nest_alloc(false); // a file that open_memory will mount as the next archive layer
        return (
            FlVfsOpenResult) { .handle = h, .is_directory = false, .error = h ? stub_vfs_ok() : stub_vfs_not_found() };
    }
    return (FlVfsOpenResult) { .handle = nullptr, .error = stub_vfs_not_found() };
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
    // Takes ownership of the VFS-allocated buffer and frees it, as a real archive plugin does.
    mi_free((void*)data);
    NestHandle* h = nest_alloc(true);
    return (FlVfsOpenResult) { .handle = h, .is_directory = true, .error = h ? stub_vfs_ok() : stub_vfs_not_found() };
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
    .get_capabilities = counting_fake_capabilities,
    .open_memory = nest_open_memory,
};

UTEST(vfs, resolve_nested_archives_close_intermediate_handles) {
    const LeakWatch leaks = leak_watch_begin(&s_nest_alloc_count, &s_nest_close_count);

    // The state holds the nested-archive stub so find_plugin_for_memory can mount each layer on the way down.
    VfsPluginEntry entry = { .plugin = &s_nest_vfs_plugin, .plugin_instance = nullptr };
    VfsState state = { 0 };
    state.plugin_first = &entry;
    state.plugin_last = &entry;

    VfsPluginEntry root_entry = { .plugin = &s_nest_vfs_plugin, .plugin_instance = nullptr };
    NestHandle* root = nest_alloc(true); // root directory the resolver walks from, and takes ownership of
    ASSERT_TRUE(root != nullptr);

    // "l0/l1" descends through two archive layers, so the resolver closes the intermediate archive1 handle when
    // it moves to archive2.
    const int root_serial = root->serial;
    PathComponents comp = path_components(s_arena, FL_S("l0/l1"));
    FinalDriver result
        = vfs_resolve_from_root(&state, (DriverHandle) { .plugin_entry = &root_entry, .handle = root }, &comp, s_arena);

    ASSERT_EQ_MSG(result.error, VfsResolveError_None, "nested-archive path must resolve");
    ASSERT_TRUE(result.handle != nullptr);
    // Compared by serial and not by address: the resolver closes the root, and the allocator may hand its address
    // straight back for the innermost archive's handle.
    ASSERT_NE_MSG(((NestHandle*)result.handle)->serial, root_serial,
                  "resolved handle is the innermost archive, not the root");

    // The resolver consumed the root. Only the final resolved handle is left for the caller to close.
    nest_close(nullptr, result.handle);

    ASSERT_EQ_MSG(leak_watch_allocated(&leaks), 5, "root + two probed files + two mounted archive layers");
    ASSERT_EQ_MSG(leak_watch_closed(&leaks), leak_watch_allocated(&leaks),
                  "descending through nested archives must close every intermediate container handle");
}

typedef struct FileResolveOutcome {
    bool root_opened;
    FinalDriver result;
    int reads;
    bool handles_balanced;
} FileResolveOutcome;

// Resolves the stub file as a plain file under state, and records the driver reads and whether every plugin handle
// was closed.
static FileResolveOutcome resolve_stub_file_as_file(VfsState* state, bool oversize) {
    s_stub_vfs_open_as_file = true;
    s_stub_vfs_oversize = oversize;
    const int reads_before = atomic_load(&s_stub_vfs_read_count);
    const LeakWatch leaks = leak_watch_begin(&s_stub_vfs_alloc_count, &s_stub_vfs_close_count);

    FlVfsOpenResult root = open_stub_root();
    FileResolveOutcome outcome = { .root_opened = root.handle != nullptr };
    if (outcome.root_opened) {
        outcome.result = resolve_stub_file_from(state, root);
    }

    s_stub_vfs_open_as_file = false;
    s_stub_vfs_oversize = false;

    outcome.reads = atomic_load(&s_stub_vfs_read_count) - reads_before;
    outcome.handles_balanced = leak_watch_balanced(&leaks);
    return outcome;
}

static bool resolve_failed_without_reading(const FileResolveOutcome* outcome, VfsResolveError expected) {
    return vfs_check(outcome->root_opened, "the stub root opens")
           && vfs_check(outcome->result.error == expected, "the resolve ends with the expected error")
           && vfs_check(outcome->reads == 0, "the file is not read")
           && vfs_check(outcome->handles_balanced, "every plugin handle is closed");
}

// A file the resolver would have to read into memory to offer to a memory driver, larger than the cap, fails the
// resolve without a single read.

UTEST(vfs, resolve_over_cap_file_fails_without_reading_it) {
    VfsPluginEntry memory_entry;
    VfsState state = memory_only_state(&memory_entry);

    const FileResolveOutcome outcome = resolve_stub_file_as_file(&state, true);

    ASSERT_TRUE(resolve_failed_without_reading(&outcome, VfsResolveError_FileTooLarge));
}

// With no driver that opens archives from memory, a file is not mountable and is not read to find that out.

UTEST(vfs, resolve_without_a_memory_driver_does_not_read_the_file) {
    VfsState state = { 0 };

    const FileResolveOutcome outcome = resolve_stub_file_as_file(&state, false);

    ASSERT_TRUE(resolve_failed_without_reading(&outcome, VfsResolveError_NotMountable));
}
