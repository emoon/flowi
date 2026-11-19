// VFS mount path tests: resolving a mount source that reaches through a root container and nested archives

#include "vfs_test_support.h"

// The resolver's segment-walking loop runs only when the root plugin matched a strict prefix of the mount path.
// This stub's open() resolves the first segment ("root") and nothing longer, so vfs_resolve_mount_path always
// enters that loop. Every exit from the loop leaves no handle live except the one returned in
// FinalDriver.handle.

typedef struct PfxHandle {
    bool is_dir;
} PfxHandle;

static _Atomic int s_pfx_alloc_count;
static _Atomic int s_pfx_close_count;

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

static bool pfx_is_directory_name(FlString path) {
    return vfs_segment_is(path, "root") || vfs_segment_is(path, "dir");
}

static bool pfx_is_file_name(FlString path) {
    return vfs_segment_is(path, "l0") || vfs_segment_is(path, "l1") || vfs_segment_is(path, "plain");
}

static bool pfx_can_open(FlString path) {
    return vfs_segment_is(path, "dir") || pfx_is_file_name(path);
}

static FlVfsOpenResult pfx_open(void* instance, void* parent_handle, FlString path, uint32_t flags,
                                FlVfsMountOptions mount_options) {
    (void)instance;
    (void)parent_handle;
    (void)flags;
    (void)mount_options;

    const bool is_dir = pfx_is_directory_name(path);
    if (!is_dir && !pfx_is_file_name(path)) {
        return (FlVfsOpenResult) { .handle = nullptr, .error = stub_vfs_not_found() };
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

    // A plugin that rejects the buffer leaves it alone, and the resolver frees it.
    if (!s_pfx_memory_mountable) {
        return (FlVfsOpenResult) { .handle = nullptr, .error = stub_vfs_not_found() };
    }

    mi_free((void*)data); // takes ownership, as a real archive plugin does
    PfxHandle* h = pfx_alloc(true);
    return (FlVfsOpenResult) { .handle = h, .is_directory = true, .error = h ? stub_vfs_ok() : stub_vfs_not_found() };
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
    .get_capabilities = counting_fake_capabilities,
    .open_memory = pfx_open_memory,
};

// Resolves path through the full mount entry point with only the prefix stub registered.
static FinalDriver pfx_mount(VfsState* state, VfsPluginEntry* entry, FlString path) {
    *entry = (VfsPluginEntry) { .plugin = &s_pfx_vfs_plugin, .plugin_instance = nullptr };
    *state = (VfsState) { 0 };
    state->plugin_first = entry;
    state->plugin_last = entry;
    return vfs_resolve_mount_path(state, path, s_arena);
}

UTEST(vfs, mount_path_directory_exit_closes_container) {
    const LeakWatch leaks = leak_watch_begin(&s_pfx_alloc_count, &s_pfx_close_count);
    s_pfx_memory_mountable = true;

    // "root" opens as the container, then "dir" consumes the rest of the path as a directory.
    VfsState state;
    VfsPluginEntry entry;
    FinalDriver result = pfx_mount(&state, &entry, FL_S("root/dir"));

    ASSERT_EQ_MSG(result.error, VfsResolveError_None, "directory at the end of the path must resolve");
    ASSERT_TRUE(result.handle != nullptr);
    // The container is already closed, and the returned handle is still usable.
    ASSERT_TRUE(pfx_is_directory(nullptr, result.handle));

    pfx_close(nullptr, result.handle);

    ASSERT_TRUE(leak_watch_balanced_after(&leaks, 2, "root container + resolved directory"));
}

UTEST(vfs, mount_path_not_mountable_exit_closes_container) {
    const LeakWatch leaks = leak_watch_begin(&s_pfx_alloc_count, &s_pfx_close_count);
    s_pfx_memory_mountable = false;

    // "plain" reads back fine but no memory plugin accepts it, so the mount fails as NotMountable.
    VfsState state;
    VfsPluginEntry entry;
    FinalDriver result = pfx_mount(&state, &entry, FL_S("root/plain"));

    ASSERT_EQ_MSG(result.error, VfsResolveError_NotMountable, "a non-archive file must not be mountable");
    ASSERT_TRUE(result.handle == nullptr);

    ASSERT_TRUE(leak_watch_balanced_after(&leaks, 2, "root container + probed file"));
}

UTEST(vfs, mount_path_missing_remainder_closes_container) {
    const LeakWatch leaks = leak_watch_begin(&s_pfx_alloc_count, &s_pfx_close_count);
    s_pfx_memory_mountable = true;

    // Nothing under "root" matches "nope", so try_open_file reports FileNotFound without opening a handle. This
    // is the one case where the resolver has no probed handle to close.
    VfsState state;
    VfsPluginEntry entry;
    FinalDriver result = pfx_mount(&state, &entry, FL_S("root/nope"));

    ASSERT_EQ_MSG(result.error, VfsResolveError_FileNotFound, "an unresolvable remainder must be FileNotFound");
    ASSERT_TRUE(result.handle == nullptr);

    ASSERT_TRUE(leak_watch_balanced_after(&leaks, 1, "only the root container was ever opened"));
}

UTEST(vfs, mount_path_nested_archives_close_root_and_containers) {
    const LeakWatch leaks = leak_watch_begin(&s_pfx_alloc_count, &s_pfx_close_count);
    s_pfx_memory_mountable = true;

    // "root/l0/l1" descends two archive layers below the root container, so the handles closed here are the root
    // and an intermediate container.
    VfsState state;
    VfsPluginEntry entry;
    FinalDriver result = pfx_mount(&state, &entry, FL_S("root/l0/l1"));

    ASSERT_EQ_MSG(result.error, VfsResolveError_None, "nested-archive path must resolve");
    ASSERT_TRUE(result.handle != nullptr);
    ASSERT_TRUE(pfx_is_directory(nullptr, result.handle));

    pfx_close(nullptr, result.handle);

    ASSERT_TRUE(leak_watch_balanced_after(&leaks, 5, "root + two probed files + two mounted archive layers"));
}

// The mount job descends through the archives nested in the mount path and keeps exactly one handle: the root of
// the innermost archive. Every container it passed through is closed, and closing the mount closes the root.

UTEST(vfs, mount_resolves_an_archive_nested_in_its_path_and_keeps_one_root_handle) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_pfx_vfs_plugin);
    s_pfx_memory_mountable = true;

    const LeakWatch leaks = leak_watch_begin(&s_pfx_alloc_count, &s_pfx_close_count);

    FlVfsMountResult mount = vfs_mount(FL_S("root/l0/l1"));
    ASSERT_EQ(mount.status, FlVfsMountErrorStatus_Success);
    vfs_wait_all();

    ASSERT_EQ((int)fl_vfs_mount_get_status(mount.mount), (int)FlVfsMountStatus_Ready);
    ASSERT_EQ_MSG(leak_watch_allocated(&leaks), 5, "root + two probed files + two mounted archive layers");
    ASSERT_EQ_MSG(leak_watch_allocated(&leaks) - leak_watch_closed(&leaks), 1,
                  "a ready mount holds only the innermost root handle");

    fl_vfs_mount_close(mount.mount);

    ASSERT_TRUE(leak_watch_balanced(&leaks));
}
