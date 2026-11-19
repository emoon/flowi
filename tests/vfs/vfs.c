// VFS mount, read and listing tests

#include "vfs_test_support.h"

// vfs_destroy closes each leaked plugin file handle exactly once and releases operation-owned heap allocations and
// listing arenas.
//
// The case does not compare mimalloc's byte counters across the cycle. mimalloc credits an allocation to the
// allocating thread and the free to the freeing thread, and a job worker's thread-local stats are not merged
// while it lives. A read-all buffer allocated on a worker and freed here therefore lowers the readable counter
// every cycle whether or not anything leaked. Heap release is checked through the close counts and
// /proc/self/maps.

// A leak adds at least one mapping per cycle, so the count may drift by less than the cycle count for reasons
// unrelated to the VFS (other threads mapping memory) without failing the case.
#define VFS_MAPS_CYCLES 24
#define VFS_MAPS_SLACK 8

// Holds while the mapping count stays within VFS_MAPS_SLACK of the count after cycle 0. Always holds where the count
// is not available.
static bool maps_within_slack(u64* first_cycle_maps, int cycle, const char* what) {
#if defined(__linux__)
    const u64 maps = count_proc_self_maps();
    if (cycle == 0) {
        *first_cycle_maps = maps;
    }
    return vfs_check(maps <= *first_cycle_maps + VFS_MAPS_SLACK, what);
#else
    (void)first_cycle_maps;
    (void)cycle;
    (void)what;
    return true;
#endif
}

UTEST(vfs, destroy_closes_leaked_handles_across_cycles) {
    u64 first_cycle_maps = 0;
    for (int cycle = 0; cycle < VFS_MAPS_CYCLES; cycle++) {
        ASSERT_TRUE_MSG(fl_vfs_init(s_arena), "VFS singleton must be uninitialized when this test starts");
        vfs_test_register_driver(&s_stub_vfs_plugin);

        LeakWatch leaks = leak_watch_begin(&s_stub_vfs_alloc_count, &s_stub_vfs_close_count);
        FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
        ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
        vfs_wait_all();
        ASSERT_TRUE(fl_vfs_mount_is_ready(mount.mount));

        // One of every operation that owns a resource is left open for vfs_destroy to release.
        FlVfsHandle file = fl_vfs_mount_open(mount.mount, FL_S(STUB_VFS_FILE), FlVfsOpenFlags_Read);
        ASSERT_NE(file, FL_VFS_HANDLE_INVALID);
        uint8_t read_buffer[sizeof(k_stub_vfs_data)] = { 0 };
        FlVfsHandle read = fl_vfs_read(file, read_buffer, sizeof(read_buffer));
        ASSERT_NE(read, FL_VFS_HANDLE_INVALID);
        FlVfsHandle read_all = stub_read_all(mount.mount);
        ASSERT_NE(read_all, FL_VFS_HANDLE_INVALID);
        FlVfsDirHandle list = fl_vfs_get_listing(mount.mount, FL_S(""), 0);
        ASSERT_NE(list, FL_VFS_DIR_HANDLE_INVALID);
        vfs_wait_all();

        ASSERT_TRUE(fl_vfs_get_data(read).success);
        ASSERT_TRUE(fl_vfs_get_data(read_all).success);
        ASSERT_TRUE(fl_vfs_get_file_list(list).success);

        vfs_destroy();

        ASSERT_GT_MSG(leak_watch_allocated(&leaks), 0, "cycle should have opened plugin handles");
        ASSERT_TRUE(leak_watch_balanced(&leaks));
        ASSERT_TRUE(maps_within_slack(&first_cycle_maps, cycle, "repeated VFS cycles must not accumulate arena reservations"));
    }
}

static void fill_pattern(u8* bytes, size_t size, unsigned multiplier, unsigned offset) {
    for (size_t i = 0; i < size; i++) {
        bytes[i] = (u8)(i * multiplier + offset);
    }
}

static bool localfs_read_matches(FlVfsMount* mount, FlString name, const u8* payload, size_t size) {
    FlVfsHandle read = fl_vfs_mount_read_all_with_options(mount, name, nullptr, nullptr, nullptr, FL_VFS_HANDLE_INVALID);
    if (read == FL_VFS_HANDLE_INVALID) {
        return false;
    }
    fl_vfs_wait(read);
    FlVfsData data = fl_vfs_get_data(read);
    bool ok = vfs_check(data.success, "read_all through built-in LocalFS should succeed")
              && vfs_check((size_t)data.size == size, "the read returns the whole file")
              && vfs_check(data.data != nullptr, "the read returns its bytes")
              && vfs_check(memcmp(data.data, payload, size) == 0, "the bytes match what was written");
    fl_vfs_close(read);
    return ok;
}

static bool localfs_listing_holds_file(FlVfsMount* mount, FlString name, size_t size) {
    FlVfsDirHandle list = fl_vfs_get_listing(mount, FL_S(""), 0);
    if (list == FL_VFS_DIR_HANDLE_INVALID) {
        return false;
    }
    fl_vfs_wait(list);
    FlVfsFileList files = fl_vfs_get_file_list(list);
    bool found = false;
    bool entry_ok = true;
    for (uint32_t i = 0; i < files.count; i++) {
        if (fl_string_equals(files.entries[i].name, name)) {
            found = true;
            entry_ok = !files.entries[i].is_directory && (size_t)files.entries[i].size == size;
        }
    }
    fl_vfs_close(list);
    return vfs_check(files.success, "listing a real directory through built-in LocalFS should succeed")
           && vfs_check(entry_ok, "the listed file is a file of the written size")
           && vfs_check(found, "listing must include the file written to the mounted directory");
}

// DirectPath: the driver returns the real OS path, so VFS core can open on-disk archives by path without
// loading them into RAM.
static bool localfs_reports_direct_path(FlArena* arena, FlString file_path) {
    const FlVfsPlugin* driver = localfs_plugin();
    if (!vfs_check((driver->get_capabilities() & FlVfsDriverCapabilities_DirectPath) != 0, "DirectPath capability")
        || !vfs_check(driver->get_file_path != nullptr, "get_file_path")) {
        return false;
    }
    FlVfsOpenResult opened = driver->open(nullptr, nullptr, file_path, FlVfsOpenFlags_Read, (FlVfsMountOptions) { 0 });
    if (!vfs_check(opened.error.vfs_status == FlVfsStatus_Success && opened.handle != nullptr, "the file opens")) {
        return false;
    }
    FlString direct = driver->get_file_path(nullptr, opened.handle, FL_S(""), arena);
    const bool matches = fl_string_equals(direct, file_path);
    driver->close(nullptr, opened.handle);
    return vfs_check(matches, "get_file_path must return the file's real OS path");
}

UTEST(vfs, builtin_localfs_reads_real_file) {
    ScratchDir scratch;
    ASSERT_TRUE(scratch_dir_create(&scratch, FL_S("unit_test_localfs_builtin_tmp")));
    FlString file_path = file_build_path_os(scratch.arena, scratch.dir, FL_S("hello.bin"));

    // A fixed pattern, so a truncated or misordered read is caught by the byte compare.
    uint8_t payload[8192];
    fill_pattern(payload, sizeof(payload), 31u, 7u);
    ASSERT_EQ((int)sizeof(payload), (int)file_write_os(file_path, payload, (i64)sizeof(payload)));

    fl_vfs_init(s_arena); // registers the built-in LocalFS driver

    FlVfsMount* mount = vfs_mount_ready(scratch.dir);
    ASSERT_TRUE_MSG(mount != nullptr, "LocalFS should mount a real directory");
    ASSERT_TRUE(localfs_read_matches(mount, FL_S("hello.bin"), payload, sizeof(payload)));
    ASSERT_TRUE(localfs_listing_holds_file(mount, FL_S("hello.bin"), sizeof(payload)));
    ASSERT_TRUE(localfs_reports_direct_path(scratch.arena, file_path));

    vfs_destroy();

    scratch_dir_destroy(&scratch);
}

// get_size and read resolve relative_path the same way: read returns the bytes of the file get_size measured.
UTEST(vfs, builtin_localfs_read_resolves_relative_path) {
    ScratchDir scratch;
    ASSERT_TRUE(scratch_dir_create(&scratch, FL_S("unit_test_localfs_relpath_tmp")));
    FlString dir_path = scratch.dir;
    FlString file_path = file_build_path_os(scratch.arena, dir_path, FL_S("child.bin"));

    uint8_t payload[64];
    fill_pattern(payload, sizeof(payload), 13u, 3u);
    ASSERT_EQ((int)sizeof(payload), (int)file_write_os(file_path, payload, (i64)sizeof(payload)));

    const FlVfsPlugin* driver = localfs_plugin();

    FlVfsOpenResult dir = driver->open(nullptr, nullptr, dir_path, 0, (FlVfsMountOptions) { 0 });
    ASSERT_EQ(dir.error.vfs_status, FlVfsStatus_Success);
    ASSERT_TRUE(dir.is_directory);

    FlVfsSizeResult sized = driver->get_size(nullptr, dir.handle, FL_S("child.bin"));
    ASSERT_EQ((int)sizeof(payload), (int)sized.size);

    uint8_t got[sizeof(payload)];
    memset(got, 0, sizeof(got));
    FlVfsReadResult read = driver->read(nullptr, dir.handle, FL_S("child.bin"), got, sized.size);
    ASSERT_EQ(read.error.vfs_status, FlVfsStatus_Success);
    ASSERT_EQ((int)sizeof(payload), (int)read.bytes_read);
    ASSERT_EQ(memcmp(got, payload, sizeof(payload)), 0);

    // Reading the directory itself fails. It does not return container bytes.
    FlVfsReadResult dir_read = driver->read(nullptr, dir.handle, FL_S(""), got, 1);
    ASSERT_EQ((int)-1, (int)dir_read.bytes_read);

    // A file handle plus a child path resolves the path. It does not fall back to the handle's own bytes.
    FlVfsOpenResult file = driver->open(nullptr, nullptr, file_path, FlVfsOpenFlags_Read, (FlVfsMountOptions) { 0 });
    ASSERT_EQ(file.error.vfs_status, FlVfsStatus_Success);
    FlVfsReadResult missing = driver->read(nullptr, file.handle, FL_S("no_such.bin"), got, 1);
    ASSERT_EQ((int)-1, (int)missing.bytes_read);
    driver->close(nullptr, file.handle);

    driver->close(nullptr, dir.handle);
    scratch_dir_destroy(&scratch);
}

static bool stub_read_returns_the_stub_bytes(FlVfsMount* mount) {
    FlVfsHandle handle = stub_read_all(mount);
    if (handle == FL_VFS_HANDLE_INVALID) {
        return false;
    }
    fl_vfs_wait(handle);
    FlVfsData data = fl_vfs_get_data(handle);
    return vfs_check(data.success, "read through generated FlVfsPlugin should succeed")
           && vfs_check((size_t)data.size == sizeof(k_stub_vfs_data), "the read returns the whole file")
           && vfs_check(data.data != nullptr, "the read returns its bytes")
           && vfs_check(memcmp(data.data, k_stub_vfs_data, sizeof(k_stub_vfs_data)) == 0, "the bytes match");
}

static bool stub_plugin_open_behaves(void) {
    StubVfsHandle root = { .is_dir = true, .offset = 0 };
    FlVfsOpenResult ok
        = s_stub_vfs_plugin.open(nullptr, &root, FL_S(STUB_VFS_FILE), FlVfsOpenFlags_Read, (FlVfsMountOptions) { 0 });
    const bool opened = vfs_check(ok.error.vfs_status == FlVfsStatus_Success && ok.handle != nullptr, "the file opens");
    if (ok.handle != nullptr) {
        s_stub_vfs_plugin.close(nullptr, ok.handle);
    }

    FlVfsOpenResult miss
        = s_stub_vfs_plugin.open(nullptr, &root, FL_S("nope.txt"), FlVfsOpenFlags_Read, (FlVfsMountOptions) { 0 });
    return opened
           && vfs_check(miss.error.vfs_status == FlVfsStatus_FileNotFound && miss.handle == nullptr,
                        "a missing name is FileNotFound without a handle");
}

UTEST(vfs, read_through_generated_plugin) {
    FlVfsMount* mount = stub_mount_ready();
    ASSERT_TRUE_MSG(mount != nullptr, "stub mount should succeed");
    ASSERT_TRUE(stub_read_returns_the_stub_bytes(mount));
    ASSERT_TRUE(stub_plugin_open_behaves());

    fl_vfs_mount_close(mount);
}

// fl_vfs_close does not deadlock when a read job is still pending. Reads are started and closed at once, many
// times, so the worker is often mid-job when close runs. A deadlock shows as a hang.

UTEST(vfs, close_during_pending_read_no_deadlock) {
    FlVfsMount* mount = stub_mount_started();
    ASSERT_TRUE_MSG(mount != nullptr, "stub mount should succeed");

    for (int i = 0; i < 64; ++i) {
        FlVfsHandle handle = stub_read_all(mount);
        ASSERT_NE(handle, FL_VFS_HANDLE_INVALID);
        fl_vfs_close(handle); // the read job may still be pending
    }

    ASSERT_TRUE_MSG(stub_read_succeeds(mount, "read after close-churn should still succeed"), "a normal read works");

    fl_vfs_mount_close(mount);
}

// fl_vfs_mount_close drains this mount's in-flight jobs and releases its leaked handles before it tears down the
// mount's root handle, strings and arena. The case starts reads and closes the mount without waiting or closing
// the handles, many times, and then checks the VFS is still usable.

UTEST(vfs, mount_close_drains_inflight_and_reaps_handles) {
    stub_vfs_begin();

    for (int i = 0; i < 32; ++i) {
        FlVfsMount* mount = vfs_mount_started(FL_S("/stub"));
        ASSERT_TRUE_MSG(mount != nullptr, "stub mount should succeed");

        // Neither waited for nor closed, so still in flight and still leaked when the mount closes.
        for (int j = 0; j < 4; ++j) {
            ASSERT_NE(stub_read_all(mount), FL_VFS_HANDLE_INVALID);
        }

        fl_vfs_mount_close(mount);
    }

    // The VFS is still usable: a new mount and read work.
    FlVfsMount* mount = vfs_mount_started(FL_S("/stub"));
    ASSERT_TRUE(mount != nullptr);
    ASSERT_TRUE_MSG(stub_read_succeeds(mount, "read after close-with-open-handles churn should still succeed"),
                    "a normal read works");
    fl_vfs_mount_close(mount);
}

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

// fl_vfs_init without an arena makes its own; vfs_destroy has to give it back.

UTEST(vfs, init_without_an_arena_frees_the_arena_it_makes) {
    u64 first_cycle_maps = 0;
    for (int cycle = 0; cycle < VFS_MAPS_CYCLES; cycle++) {
        ASSERT_TRUE(fl_vfs_init(nullptr));
        vfs_destroy();
        ASSERT_TRUE(maps_within_slack(&first_cycle_maps, cycle, "each cycle must give back its own arena"));
    }
}

// A mount resolves its source once and publishes one status. A source that does not resolve leaves the mount in
// the error state for good, and every operation against it fails without touching a root handle.

UTEST(vfs, mount_of_a_missing_source_reports_error_and_its_ops_fail) {
    fl_vfs_init(s_arena); // idempotent

    FlVfsMountResult mount = vfs_mount(FL_S("unit_test_vfs_no_such_source/none"));
    ASSERT_EQ(mount.status, FlVfsMountErrorStatus_Success);
    vfs_wait_all();

    ASSERT_EQ((int)fl_vfs_mount_get_status(mount.mount), (int)FlVfsMountStatus_Error);
    ASSERT_FALSE(fl_vfs_mount_is_ready(mount.mount));

    FlVfsHandle read = fl_vfs_mount_read_all_with_options(mount.mount, FL_S("x.bin"), nullptr, nullptr, nullptr,
                                                          FL_VFS_HANDLE_INVALID);
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

// A listing asks the driver, sorts directories first and then in natural order, and flags the files an archive
// driver claims by name without opening any of them. LocalFS is the mount's driver here, so a stand-in driver
// that claims ".zip" names does the recognizing.

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
    return (FlVfsOpenResult) { .handle = nullptr, .error = stub_vfs_not_found() };
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

static bool listing_fixture_create(ScratchDir* scratch) {
    FlArena* arena = scratch->arena;
    FlString dir = scratch->dir;
    return file_create_directory_os(file_build_path_os(arena, dir, FL_S("zdir")))
           && file_create_directory_os(file_build_path_os(arena, dir, FL_S("adir")))
           && write_listing_fixture_file(dir, FL_S("b10.txt")) && write_listing_fixture_file(dir, FL_S("b2.txt"))
           && write_listing_fixture_file(dir, FL_S("a.zip"))
           && write_listing_fixture_file(file_build_path_os(arena, dir, FL_S("adir")), FL_S("inner.txt"));
}

// The listing of path holds exactly these names, in this order.
static bool listing_names_are(FlVfsMount* mount, FlString path, const char* const* names, u32 count) {
    FlVfsDirHandle handle = fl_vfs_get_listing(mount, path, 0);
    fl_vfs_wait(handle);
    FlVfsFileList files = fl_vfs_get_file_list(handle);
    bool ok = vfs_check(files.success, "the listing succeeds") && vfs_check(files.count == count, "the entry count");
    for (u32 i = 0; ok && i < count; i++) {
        ok = vfs_check(fl_string_equals(files.entries[i].name, string_from_cstr(names[i])),
                       "entries are directories first, then natural order");
    }
    fl_vfs_close(handle);
    return ok;
}

// Only the first two entries are directories and only the third is an archive.
static bool listing_root_flags_are_right(FlVfsMount* mount) {
    FlVfsDirHandle handle = fl_vfs_get_listing(mount, FL_S(""), 0);
    fl_vfs_wait(handle);
    FlVfsFileList files = fl_vfs_get_file_list(handle);
    bool ok = files.success;
    for (u32 i = 0; ok && i < files.count; i++) {
        ok = vfs_check(files.entries[i].is_directory == (i < 2), "only the first two entries are directories")
             && vfs_check(files.entries[i].is_archive == (i == 2), "only the name the archive driver claims is an archive");
    }
    fl_vfs_close(handle);
    return ok;
}

UTEST(vfs, listing_puts_directories_first_in_natural_order_and_flags_archives_by_name) {
    ScratchDir scratch;
    ASSERT_TRUE(scratch_dir_create(&scratch, FL_S("unit_test_vfs_listing_tmp")));
    ASSERT_TRUE(listing_fixture_create(&scratch));

    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_arcname_vfs_plugin);

    FlVfsMount* mount = vfs_mount_ready(scratch.dir);
    ASSERT_TRUE(mount != nullptr);

    const char* const root_names[] = { "adir", "zdir", "a.zip", "b2.txt", "b10.txt" };
    ASSERT_TRUE(listing_names_are(mount, FL_S(""), root_names, 5));
    ASSERT_TRUE(listing_root_flags_are_right(mount));

    const char* const inner_names[] = { "inner.txt" };
    ASSERT_TRUE(listing_names_are(mount, FL_S("adir"), inner_names, 1));

    // A directory that is not there lists as empty.
    ASSERT_TRUE(listing_names_are(mount, FL_S("no_such_dir"), nullptr, 0));

    fl_vfs_mount_close(mount);
    scratch_dir_destroy(&scratch);
}
