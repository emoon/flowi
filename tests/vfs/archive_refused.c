// VFS tree tests: loading an archive onto a tree node when its driver refuses it

#include "vfs_test_support.h"

// Loading an archive onto a tree node. The stand-in archive driver takes ownership of the buffer open_memory is
// handed, as a real one does, and frees it when the handle closes.

typedef struct OvrHandle {
    uint8_t* mem; // VFS-allocated buffer this handle owns
} OvrHandle;

static _Atomic int s_ovr_alloc_count;
static _Atomic int s_ovr_close_count;
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

static FlVfsPlugin s_ovr_refusing_vfs_plugin = {
    .api_version = FL_VFS_PLUGIN_API_VERSION,
    .plugin_name = FL_S_("OvrRefusingArc"),
    .can_open = ovr_can_open,
    .read = ovr_read,
    .is_directory = ovr_is_directory,
    .close = ovr_close,
    .get_capabilities = counting_fake_capabilities,
    .open_memory = ovr_open_memory_refusing,
    .get_file_path = nullptr,
};

// A driver that refuses the archive leaves the buffer the VFS read for it with the VFS, which frees it once.

#if USE_MIMALLOC
static int s_mi_error_count;

static void count_mi_error(int err, void* arg) {
    (void)err;
    (void)arg;
    s_mi_error_count++;
}

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

// Starts counting the errors mimalloc reports, and returns the count so far.
static int mimalloc_errors_begin(void) {
#if USE_MIMALLOC
    const int before = s_mi_error_count;
    mi_register_error(count_mi_error, nullptr);
    return before;
#else
    return 0;
#endif
}

// No error was reported since errors_before, and the buffer the driver refused is no longer a live heap block.
static bool refused_buffer_released_once(int errors_before) {
#if USE_MIMALLOC
    mi_register_error(nullptr, nullptr);
    BlockProbe probe = { .block = s_ovr_refused_data };
    mi_heap_visit_blocks(mi_heap_get_default(), true, probe_block, &probe);
    return vfs_check(s_mi_error_count - errors_before == 0, "the buffer must not be freed twice")
           && vfs_check(!probe.found, "the buffer the driver refused must have been freed");
#else
    (void)errors_before;
    return true;
#endif
}

static bool refusal_left_node_untouched(bool loaded, const VfsTreeNode* node) {
    return vfs_check(!loaded, "a refused archive does not load")
           && vfs_check(node->handle == nullptr && node->plugin_entry == nullptr, "the node holds no driver handle")
           && vfs_check(!node->is_directory, "the node is not turned into a directory");
}

UTEST(vfs, archive_memory_load_refused_leaves_the_node_untouched_and_frees_the_buffer_once) {
    const LeakWatch leaks = leak_watch_begin(&s_ovr_alloc_count, &s_ovr_close_count);
    const int frees_before = s_ovr_mem_free_count;

    FlArena* arena = arena_new();
    FlVfsMount mount = { .nodes_arena = arena };
    VfsPluginEntry entry = { .plugin = &s_ovr_refusing_vfs_plugin, .plugin_instance = nullptr };
    VfsState state = { .plugin_first = &entry, .plugin_last = &entry };
    VfsTreeNode* node = vfs_tree_create_node(&mount, nullptr, FL_S("archive.zip"));
    OvrHandle* parent_handle = ovr_alloc(nullptr);

    const int mi_errors_before = mimalloc_errors_begin();
    const ArchiveFile file
        = { .container = { .plugin_entry = &entry, .handle = parent_handle }, .path = FL_S("archive.zip"), .size = 32 };
    bool ok = vfs_tree_try_load_as_archive(&state, node, &file, arena);

    ASSERT_TRUE(refused_buffer_released_once(mi_errors_before));
    ASSERT_TRUE(refusal_left_node_untouched(ok, node));
    ASSERT_EQ_MSG(s_ovr_mem_free_count - frees_before, 0, "a refused buffer is not the driver's to free");

    ovr_close(nullptr, parent_handle);
    vfs_tree_cleanup_node(node);
    fl_arena_destroy(arena);

    ASSERT_TRUE(leak_watch_balanced(&leaks));
}
