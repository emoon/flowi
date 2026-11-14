// OS abstraction, path, log and file-watcher tests

#include "unit_tests_common.h"

UTEST(file_watcher, full_registry_rejects_instead_of_corrupting) {
    FlFileWatcherConfig config = { .watch_directories = true };
    FlFileWatcherHandle handles[64];
    u32 n = 0;
    FlFileWatcherHandle h = FL_FILE_WATCHER_INVALID;

    while (n < (u32)sizeof_array(handles)) {
        h = file_watcher_start(FL_S("."), config); // "." always exists, and each start is a separate watcher
        if (h == FL_FILE_WATCHER_INVALID) {
            break;
        }
        handles[n++] = h;
    }

    ASSERT_EQ(FL_FILE_WATCHER_INVALID, h);
    ASSERT_TRUE(n <= 16); // the registry holds 16 watchers

    for (u32 i = 0; i < n; i++) {
        file_watcher_stop(handles[i]);
    }
}

// file_watcher_path_is_direct_child implements recursive=false on platforms whose native watch API is always
// recursive (FSEvents on macOS). The callback keeps events for the watched root and its immediate children and
// drops deeper ones.

UTEST(file_watcher, path_is_direct_child_accepts_root_and_immediate_children) {
    // The root itself (e.g. attribute changes on the watched directory)
    ASSERT_TRUE(file_watcher_path_is_direct_child(FL_S("/watch/dir"), FL_S("/watch/dir")));
    ASSERT_TRUE(file_watcher_path_is_direct_child(FL_S("/watch/dir/"), FL_S("/watch/dir")));
    ASSERT_TRUE(file_watcher_path_is_direct_child(FL_S("/watch/dir"), FL_S("/watch/dir/")));

    // Immediate children, with and without trailing separators
    ASSERT_TRUE(file_watcher_path_is_direct_child(FL_S("/watch/dir"), FL_S("/watch/dir/file.txt")));
    ASSERT_TRUE(file_watcher_path_is_direct_child(FL_S("/watch/dir/"), FL_S("/watch/dir/file.txt")));
    ASSERT_TRUE(file_watcher_path_is_direct_child(FL_S("/watch/dir"), FL_S("/watch/dir/subdir/")));

    // Filesystem root as the watched path
    ASSERT_TRUE(file_watcher_path_is_direct_child(FL_S("/"), FL_S("/")));
    ASSERT_TRUE(file_watcher_path_is_direct_child(FL_S("/"), FL_S("/etc")));
}

UTEST(file_watcher, path_is_direct_child_rejects_deep_and_unrelated_paths) {
    // Deeper than one level
    ASSERT_FALSE(file_watcher_path_is_direct_child(FL_S("/watch/dir"), FL_S("/watch/dir/sub/file.txt")));
    ASSERT_FALSE(file_watcher_path_is_direct_child(FL_S("/"), FL_S("/etc/hosts")));

    // Outside the root entirely
    ASSERT_FALSE(file_watcher_path_is_direct_child(FL_S("/watch/dir"), FL_S("/other/file.txt")));
    ASSERT_FALSE(file_watcher_path_is_direct_child(FL_S("/watch/dir"), FL_S("/watch")));

    // Sibling whose name shares the root as a prefix
    ASSERT_FALSE(file_watcher_path_is_direct_child(FL_S("/watch/dir"), FL_S("/watch/dirt")));
    ASSERT_FALSE(file_watcher_path_is_direct_child(FL_S("/watch/dir"), FL_S("/watch/dirt/file.txt")));

    // Degenerate inputs
    ASSERT_FALSE(file_watcher_path_is_direct_child(FL_S(""), FL_S("/watch/dir")));
    ASSERT_FALSE(file_watcher_path_is_direct_child(FL_S("/watch/dir"), FL_S("")));
}

// POSIX readdir reuses its internal dirent buffer, so dir_read_next_os copies each name into the caller's arena,
// as the Windows version does. The name stays valid across later reads and the close of the handle.

UTEST(os_dir, entry_name_survives_next_read_and_close) {
    FlArena* arena = arena_new();

    FlString dir_path = FL_S("unit_test_dir_iter_tmp");
    file_remove_directory_recursive_os(dir_path); // clean any leftover from an aborted run
    ASSERT_TRUE(file_create_directory_os(dir_path));
    ASSERT_EQ(1, (int)file_write_os(FL_S("unit_test_dir_iter_tmp/aaaa.txt"), (const u8*)"a", 1));
    ASSERT_EQ(1, (int)file_write_os(FL_S("unit_test_dir_iter_tmp/bbbb.txt"), (const u8*)"b", 1));

    DirHandle dir = dir_open_os(arena, dir_path);
    ASSERT_TRUE(dir.is_valid);

    FileStat first = dir_read_next_os(arena, &dir);
    ASSERT_TRUE(first.is_valid);
    FlString snapshot = fl_string_copy(arena, first.name);

    FileStat second = dir_read_next_os(arena, &dir);
    ASSERT_TRUE(second.is_valid);
    ASSERT_FALSE(fl_string_equals(first.name, second.name)); // distinct entries, distinct storage

    dir_close_os(&dir);
    ASSERT_TRUE(fl_string_equals(first.name, snapshot)); // still intact after next read + close

    file_remove_directory_recursive_os(dir_path);
    fl_arena_destroy(arena);
}

// file_get_size_os returns -1 when stat() fails. file_read_from_handle_os has to reject that value, because
// min(buffer_size, -1) would reach read() as SIZE_MAX. A stat failure after a successful open cannot be produced
// on demand, so the test passes -1 directly.

UTEST(os_file, negative_file_size_fails_without_reading) {
    FlString path = FL_S("unit_test_negative_size_tmp.txt");
    ASSERT_EQ(4, (int)file_write_os(path, (const u8*)"data", 4));

    const i64 fd = file_open_os(path, 0);
    ASSERT_TRUE(fd >= 0);

    u8 buffer[4];
    memset(buffer, 0xAB, sizeof(buffer));

    ASSERT_EQ(-1, (int)file_read_from_handle_os(buffer, fd, -1, (i64)sizeof(buffer)));
    for_count(i, (int)sizeof(buffer)) {
        ASSERT_EQ(0xAB, buffer[i]); // no read happened, so dest is untouched
    }

    file_close_handle_os(fd);
    file_delete_os(path);
}

#if !PLATFORM_WINDOWS

// get_disk_space_os walks up the directory tree and retries statvfs() on shorter prefixes. The seam makes every
// statvfs() fail, so the walk goes past the root.

UTEST(os_disk_space, statvfs_failure_all_the_way_to_root_terminates) {
    os_disk_space_test_force_statvfs_failure(true);
    const DiskSpaceInfo info = get_disk_space_os(FL_S("/flowi_nonexistent_dir/sub/target.bin"));
    os_disk_space_test_force_statvfs_failure(false);

    ASSERT_FALSE(info.is_valid);
}

UTEST(os_disk_space, root_path_statvfs_failure_terminates) {
    os_disk_space_test_force_statvfs_failure(true);
    const DiskSpaceInfo info = get_disk_space_os(FL_S("/"));
    os_disk_space_test_force_statvfs_failure(false);

    ASSERT_FALSE(info.is_valid);
}

UTEST(os_disk_space, walk_up_finds_existing_ancestor) {
    // The leaf components do not exist, so the walk up lands on an existing ancestor ("/tmp" or "/") and reports
    // its filesystem.
    const DiskSpaceInfo info = get_disk_space_os(FL_S("/tmp/flowi_nonexistent_dir/sub/target.bin"));

    ASSERT_TRUE(info.is_valid);
    ASSERT_TRUE(info.bytes_total > 0);
}

UTEST(os_disk_space, existing_path_immediate_hit) {
    const DiskSpaceInfo info = get_disk_space_os(FL_S("/tmp"));

    ASSERT_TRUE(info.is_valid);
    ASSERT_TRUE(info.bytes_total > 0);
}

#endif // !PLATFORM_WINDOWS

// file_create_directory_recursive_os checks that each path component exists before creating it, so another
// creator can win the race between the check and the create. Two threads meet at each iteration and create the
// same nested path at once, and every call has to succeed.

#define MKDIR_RACE_ITERATIONS 200

typedef struct MkdirRaceCtx {
    atomic_int gate;
    atomic_int failures;
} MkdirRaceCtx;

static void* mkdir_race_thread(void* user_data) {
    MkdirRaceCtx* ctx = user_data;
    char path[128];
    for (int i = 0; i < MKDIR_RACE_ITERATIONS; i++) {
        // Both threads arrive here before either starts this iteration's create.
        atomic_fetch_add(&ctx->gate, 1);
        while (atomic_load(&ctx->gate) < 2 * (i + 1)) {}
        snprintf(path, sizeof(path), "flowi_mkdir_race_tmp/%d/a/b/c", i);
        if (!file_create_directory_recursive_os(string_from_cstr(path))) {
            atomic_fetch_add(&ctx->failures, 1);
        }
    }
    return nullptr;
}

UTEST(os_file, recursive_mkdir_survives_concurrent_creator) {
    file_remove_directory_recursive_os(FL_S("flowi_mkdir_race_tmp"));

    MkdirRaceCtx ctx = { 0 };
    Thread thread_a;
    Thread thread_b;
    ASSERT_TRUE(os_thread_create(&thread_a, mkdir_race_thread, &ctx));
    ASSERT_TRUE(os_thread_create(&thread_b, mkdir_race_thread, &ctx));
    os_thread_join(&thread_a);
    os_thread_join(&thread_b);

    ASSERT_EQ(0, atomic_load(&ctx.failures));

    char last_path[128];
    snprintf(last_path, sizeof(last_path), "flowi_mkdir_race_tmp/%d/a/b/c", MKDIR_RACE_ITERATIONS - 1);
    ASSERT_TRUE(file_directory_exists_os(string_from_cstr(last_path)));

    file_remove_directory_recursive_os(FL_S("flowi_mkdir_race_tmp"));
}

// A real failure is still reported. A regular file in place of a path component makes the create fail, and the
// directory check after the failure is false.

UTEST(os_file, recursive_mkdir_still_fails_on_file_component) {
    file_remove_directory_recursive_os(FL_S("flowi_mkdir_fail_tmp"));
    ASSERT_TRUE(file_create_directory_recursive_os(FL_S("flowi_mkdir_fail_tmp")));
    ASSERT_EQ(4, (int)file_write_os(FL_S("flowi_mkdir_fail_tmp/blocker"), (const u8*)"data", 4));

    ASSERT_FALSE(file_create_directory_recursive_os(FL_S("flowi_mkdir_fail_tmp/blocker/sub")));

    file_remove_directory_recursive_os(FL_S("flowi_mkdir_fail_tmp"));
}

// Re-initializing the log file closes the previous init's fd before opening the new one, so count_open_fds
// returns the same value before and after the second init.

// Other threads log into the same file, so the case's own line can sit well past the start.
#define LOG_FILE_READ_CAP MB(4)

UTEST(log_file, reinit_closes_previous_fd) {
    FlString dir = FL_S("/tmp/flowi_log_reinit_test");
    file_remove_directory_recursive_os(dir); // clean any leftover from an aborted run

    ASSERT_TRUE(fl_log_file_init(dir));

#if defined(__linux__)
    const int fds_after_first_init = count_open_fds();
#endif

    ASSERT_TRUE(fl_log_file_init(dir));

#if defined(__linux__)
    ASSERT_EQ(fds_after_first_init, count_open_fds());
#endif

    // A message logged now lands in the file the second init opened.
    fl_log_message(LogLevel_Fatal, __FILE__, __LINE__, "log reinit smoke %d", 42);

    arena_scratch_auto(temp);
    u8* buffer = arena_alloc_array_zero(temp.arena, u8, LOG_FILE_READ_CAP);
    const i64 bytes = file_read_os(buffer, fl_log_file_get_path(), (i64)LOG_FILE_READ_CAP - 1);
    ASSERT_TRUE(bytes > 0);
    ASSERT_TRUE(strstr((const char*)buffer, "log reinit smoke 42") != nullptr);

    file_remove_directory_recursive_os(dir);
}

// FlString has an explicit length and no guaranteed trailing NUL. fl_string_to_cstr gives C APIs the
// NUL-terminated log file path they need.

UTEST(log_file, path_converts_to_valid_cstr) {
    FlString dir = FL_S("/tmp/flowi_log_cstr_test");
    file_remove_directory_recursive_os(dir); // clean any leftover from an aborted run

    ASSERT_TRUE(fl_log_file_init(dir));

    FlString log_path = fl_log_file_get_path();
    ASSERT_TRUE(log_path.length > 0);

    FlArena* arena = arena_new();
    const char* log_path_cstr = fl_string_to_cstr(arena, log_path);

    ASSERT_EQ((u64)log_path.length, (u64)strlen(log_path_cstr));
    ASSERT_EQ(0, memcmp(log_path_cstr, log_path.data, log_path.length));

    // The converted path names the file the logger writes to.
    fl_log_message(LogLevel_Fatal, __FILE__, __LINE__, "cstr path smoke %d", 7);
    u8* buffer = arena_alloc_array_zero(arena, u8, LOG_FILE_READ_CAP);
    const i64 bytes = file_read_os(buffer, string_from_cstr(log_path_cstr), (i64)LOG_FILE_READ_CAP - 1);
    ASSERT_TRUE(bytes > 0);
    ASSERT_TRUE(strstr((const char*)buffer, "cstr path smoke 7") != nullptr);

    fl_arena_destroy(arena);
    file_remove_directory_recursive_os(dir);
}

// The data dir, log file name and file-watcher queue default to neutral "flowi" values. A host override replaces
// only the fields it sets, and fl_log_file_init takes the log file name from the app identity.

UTEST(app_identity, defaults_override_and_log_routing) {
    ASSERT_TRUE(fl_string_equals(fl_app_identity_data_dir_name(), FL_S(".flowi")));
    ASSERT_TRUE(fl_string_equals(fl_app_identity_log_file_name(), FL_S("flowi.log")));
    ASSERT_TRUE(fl_string_equals(fl_app_identity_file_watcher_queue(), FL_S("org.flowi.filewatcher")));

    // A partial override changes only the non-empty fields.
    FlAppIdentity partial = { .log_file_name = FL_S("myapp.log") };
    fl_app_identity_set(&partial);
    ASSERT_TRUE(fl_string_equals(fl_app_identity_log_file_name(), FL_S("myapp.log")));
    ASSERT_TRUE(fl_string_equals(fl_app_identity_data_dir_name(), FL_S(".flowi")));
    ASSERT_TRUE(fl_string_equals(fl_app_identity_file_watcher_queue(), FL_S("org.flowi.filewatcher")));

    // fl_log_file_init builds the log file from the overridden name, so the override shows up on disk.
    FlString dir = FL_S("/tmp/flowi_app_identity_test");
    file_remove_directory_recursive_os(dir);
    ASSERT_TRUE(fl_log_file_init(dir));
    FlArena* path_arena = arena_new();
    FlString expected = file_build_path_os(path_arena, dir, FL_S("myapp.log"));
    ASSERT_TRUE(fl_string_equals(fl_log_file_get_path(), expected));
    fl_arena_destroy(path_arena);
    file_remove_directory_recursive_os(dir);

    FlAppIdentity replacement = { .log_file_name = FL_S("other.log") };
    fl_app_identity_set(&replacement);
    ASSERT_TRUE(fl_string_equals(fl_app_identity_log_file_name(), FL_S("other.log")));
    ASSERT_TRUE(fl_string_equals(fl_app_identity_data_dir_name(), FL_S(".flowi")));
}

UTEST(app_identity, data_dir_name_routes_through_seam) {
    FlAppIdentity ident = { .data_dir_name = FL_S(".flowi_datadirtest") };
    fl_app_identity_set(&ident);
    ASSERT_TRUE(fl_string_equals(fl_app_identity_data_dir_name(), FL_S(".flowi_datadirtest")));

    FlAppIdentity replacement = { .data_dir_name = FL_S(".flowi_other") };
    fl_app_identity_set(&replacement);
    ASSERT_TRUE(fl_string_equals(fl_app_identity_data_dir_name(), FL_S(".flowi_other")));
}

// Stopping a watcher leaves every other watcher's state where it is. Platform layers (FSEvents on macOS) keep the
// FileWatcherState* passed to platform_start_watching for the stream's lifetime, so removal tombstones the slot
// in place and does not compact the array.

UTEST(file_watcher, stop_does_not_relocate_live_watchers) {
    FlFileWatcherConfig config = { .watch_directories = true };
    FlFileWatcherHandle h1 = file_watcher_start(FL_S("."), config);
    FlFileWatcherHandle h2 = file_watcher_start(FL_S("."), config);
    FlFileWatcherHandle h3 = file_watcher_start(FL_S("."), config);
    ASSERT_NE(FL_FILE_WATCHER_INVALID, h1);
    ASSERT_NE(FL_FILE_WATCHER_INVALID, h2);
    ASSERT_NE(FL_FILE_WATCHER_INVALID, h3);

    FileWatcherState* before_h2 = find_watcher_state(h2);
    FileWatcherState* before_h3 = find_watcher_state(h3);
    ASSERT_TRUE(before_h2 != nullptr);
    ASSERT_TRUE(before_h3 != nullptr);

    file_watcher_stop(h1);

    ASSERT_EQ(before_h2, find_watcher_state(h2));
    ASSERT_EQ(before_h3, find_watcher_state(h3));
    ASSERT_TRUE(file_watcher_is_active(h2));
    ASSERT_TRUE(file_watcher_is_active(h3));

    file_watcher_stop(h2);
    file_watcher_stop(h3);
}

// Freed slots are reused: start/stop cycles far beyond the 16-slot registry capacity never hit the "registry
// full" rejection.

UTEST(file_watcher, slots_reused_across_stop_start_cycles) {
    FlFileWatcherConfig config = { .watch_directories = true };
    for (int i = 0; i < 100; i++) {
        FlFileWatcherHandle h = file_watcher_start(FL_S("."), config);
        ASSERT_NE(FL_FILE_WATCHER_INVALID, h);
        file_watcher_stop(h);
    }
}

// Once the registry is full, stopping one watcher frees exactly one slot for a later start.

UTEST(file_watcher, full_registry_frees_slot_on_stop) {
    FlFileWatcherConfig config = { .watch_directories = true };
    FlFileWatcherHandle handles[64];
    u32 n = 0;

    while (n < (u32)sizeof_array(handles)) {
        FlFileWatcherHandle h = file_watcher_start(FL_S("."), config);
        if (h == FL_FILE_WATCHER_INVALID) {
            break;
        }
        handles[n++] = h;
    }
    ASSERT_TRUE(n > 0); // registry filled to capacity

    file_watcher_stop(handles[0]);

    FlFileWatcherHandle reused = file_watcher_start(FL_S("."), config);
    ASSERT_NE(FL_FILE_WATCHER_INVALID, reused);
    handles[0] = reused;

    for (u32 i = 0; i < n; i++) {
        file_watcher_stop(handles[i]);
    }
}

// add_file_change runs on platform threads and iterates watcher->callbacks under changes_lock. This case
// registers and unregisters callbacks while it dispatches. Without the lock ThreadSanitizer reports a race, and
// the run can crash.

typedef struct FwCallbackRaceCtx {
    FileWatcherState* watcher;
    atomic_bool stop;
    atomic_uint invoked;
} FwCallbackRaceCtx;

static void fw_race_on_change(const FileChange* change, void* user_data) {
    (void)change;
    atomic_fetch_add(&((FwCallbackRaceCtx*)user_data)->invoked, 1);
}

static void* fw_race_change_thread(void* user_data) {
    FwCallbackRaceCtx* ctx = user_data;
    while (!atomic_load(&ctx->stop)) {
        add_file_change(ctx->watcher, FL_S("fw_race_target.txt"), (FlString) { 0 }, FlFileChangeType_Modified, false);
    }
    return nullptr;
}

UTEST(file_watcher, callback_registration_does_not_race_dispatch) {
    FlFileWatcherConfig config = { .watch_directories = true };
    FlFileWatcherHandle handle = file_watcher_start(FL_S("."), config);
    ASSERT_NE(FL_FILE_WATCHER_INVALID, handle);

    FwCallbackRaceCtx ctx = { 0 };
    ctx.watcher = find_watcher_state(handle);
    ASSERT_TRUE(ctx.watcher != nullptr);

    Thread change_thread;
    ASSERT_TRUE(os_thread_create(&change_thread, fw_race_change_thread, &ctx));

    for (int i = 0; i < 5000; i++) {
        FileWatcherCallbackHandle cb
            = file_watcher_register_callback(handle, FL_S("fw_race_target.txt"), fw_race_on_change, &ctx);
        ASSERT_NE(FILE_WATCHER_CALLBACK_INVALID, cb);
        file_watcher_unregister_callback(handle, cb);
    }

    atomic_store(&ctx.stop, true);
    os_thread_join(&change_thread);

    ASSERT_TRUE(file_watcher_is_active(handle));
    file_watcher_stop(handle);
}

// Per-event path strings live in a per-watcher arena that is rewound whenever the change ring is
// drained.

typedef struct FwArenaCbCtx {
    FlString expected;
    bool saw_valid_path;
} FwArenaCbCtx;

static void fw_arena_on_change(const FileChange* change, void* user_data) {
    FwArenaCbCtx* ctx = user_data;
    ctx->saw_valid_path = fl_string_equals(change->path, ctx->expected);
}

UTEST(file_watcher, change_string_arena_rewinds_on_drain) {
    FlFileWatcherConfig config = { .watch_directories = true };
    FlFileWatcherHandle handle = file_watcher_start(FL_S("."), config);
    ASSERT_NE(FL_FILE_WATCHER_INVALID, handle);

    FileWatcherState* watcher = find_watcher_state(handle);
    ASSERT_TRUE(watcher != nullptr);

    FlArena* caller_arena = arena_new();
    FileChangeIterator first_drain = { 0 };
    u64 pos_after_first_drain = 0;

    for (int cycle = 0; cycle < 8; cycle++) {
        char names[32][32];
        for (int i = 0; i < 32; i++) {
            // Distinct paths per event so nothing coalesces and every add allocates
            int len = snprintf(names[i], sizeof(names[i]), "fw_arena_c%d_f%d.txt", cycle, i);
            FlString path = { .data = names[i], .length = (u32)len };
            add_file_change(watcher, path, (FlString) { 0 }, FlFileChangeType_Modified, false);
        }

        FileChangeIterator iter = file_watcher_get_changes(handle, caller_arena);
        ASSERT_EQ(32u, iter.count);

        u64 pos = atomic_load_explicit(&watcher->changes_arena->pos, memory_order_relaxed);
        if (cycle == 0) {
            first_drain = iter;
            pos_after_first_drain = pos;
        } else {
            // The traffic is identical each cycle, so any growth means drained strings were not reclaimed
            ASSERT_EQ(pos_after_first_drain, pos);
        }
    }

    // Strings from an earlier drain live in the caller's arena, so they are intact after the internal arena has
    // been rewound and refilled seven times.
    ASSERT_TRUE(fl_string_equals(first_drain.changes[0].path, FL_S("fw_arena_c0_f0.txt")));
    ASSERT_TRUE(fl_string_equals(first_drain.changes[31].path, FL_S("fw_arena_c0_f31.txt")));

    // clear_changes is the other drain path and reclaims too
    add_file_change(watcher, FL_S("fw_arena_clear.txt"), (FlString) { 0 }, FlFileChangeType_Modified, false);
    file_watcher_clear_changes(handle);
    ASSERT_EQ(pos_after_first_drain, atomic_load_explicit(&watcher->changes_arena->pos, memory_order_relaxed));

    // Callbacks fire during add_file_change, before any rewind, so the path is valid there
    FwArenaCbCtx cb_ctx = { .expected = FL_S("fw_arena_cb.txt") };
    FileWatcherCallbackHandle cb
        = file_watcher_register_callback(handle, FL_S("fw_arena_cb.txt"), fw_arena_on_change, &cb_ctx);
    ASSERT_NE(FILE_WATCHER_CALLBACK_INVALID, cb);
    add_file_change(watcher, FL_S("fw_arena_cb.txt"), (FlString) { 0 }, FlFileChangeType_Modified, false);
    ASSERT_TRUE(cb_ctx.saw_valid_path);
    file_watcher_unregister_callback(handle, cb);

    file_watcher_stop(handle); // releases the per-watcher string arena
    fl_arena_destroy(caller_arena);
}

// A directory moved into a recursively watched tree with rename() arrives as IN_MOVED_TO, not IN_CREATE. The
// inotify backend adds a watch for it as it does for a new directory. Without that watch, changes inside the
// moved-in subtree produce no events.

typedef struct FwMovedInCtx {
    bool fired;
} FwMovedInCtx;

#if defined(__linux__)
static void fw_moved_in_on_change(const FileChange* change, void* user_data) {
    (void)change;
    ((FwMovedInCtx*)user_data)->fired = true;
}
#endif

UTEST(file_watcher, moved_in_directory_becomes_watched) {
#if defined(__linux__)
    file_remove_directory_recursive_os(FL_S("fw_moved_root")); // clean any leftover from an aborted run
    file_remove_directory_recursive_os(FL_S("fw_moved_src"));
    ASSERT_TRUE(file_create_directory_os(FL_S("fw_moved_root")));
    ASSERT_TRUE(file_create_directory_os(FL_S("fw_moved_src")));

    FlFileWatcherConfig config = { .recursive = true, .watch_files = true, .watch_directories = true };
    FlFileWatcherHandle handle = file_watcher_start(FL_S("fw_moved_root"), config);
    ASSERT_NE(FL_FILE_WATCHER_INVALID, handle);

    FwMovedInCtx ctx = { 0 };
    FileWatcherCallbackHandle cb = file_watcher_register_callback(handle, FL_S("fw_moved_root/fw_moved_src/inner.txt"),
                                                                  fw_moved_in_on_change, &ctx);
    ASSERT_NE(FILE_WATCHER_CALLBACK_INVALID, cb);

    // Move the directory into the watched tree. The poll handles IN_MOVED_TO and adds a watch for the moved-in
    // directory before anything happens inside it.
    ASSERT_TRUE(file_rename_os(FL_S("fw_moved_src"), FL_S("fw_moved_root/fw_moved_src")));
    file_watcher_has_changes(handle); // poll: consumes IN_MOVED_TO, adds the subtree watch

    // A file created inside the moved-in directory reaches the registered callback
    ASSERT_EQ(1, (int)file_write_os(FL_S("fw_moved_root/fw_moved_src/inner.txt"), (const u8*)"x", 1));
    file_watcher_has_changes(handle); // poll: the create/modify events fire the callback
    ASSERT_TRUE(ctx.fired);

    file_watcher_unregister_callback(handle, cb);
    file_watcher_stop(handle);
    file_remove_directory_recursive_os(FL_S("fw_moved_root"));
#else
    UTEST_SKIP("inotify move-in recursion is Linux-only");
#endif
}

// fl_path_normalize_full does standard lexical normalization, like Go's path.Clean. An absolute input stays
// absolute and ".." at the root is dropped, because "/" has no parent. A relative input keeps the ".." segments
// it cannot pop, so the result still points above its unknown base.

static bool path_normalize_full_matches(FlString input, FlString expected) {
    arena_scratch_auto(temp);
    return fl_string_equals(fl_path_normalize_full(temp.arena, input), expected);
}

UTEST(path, normalize_full_preserves_absolute_root) {
    ASSERT_TRUE(path_normalize_full_matches(FL_S("/a/b/../c"), FL_S("/a/c")));
    ASSERT_TRUE(path_normalize_full_matches(FL_S("/a/./b"), FL_S("/a/b")));
    ASSERT_TRUE(path_normalize_full_matches(FL_S("/"), FL_S("/")));

    // ".." at the root is dropped.
    ASSERT_TRUE(path_normalize_full_matches(FL_S("/../a"), FL_S("/a")));
    ASSERT_TRUE(path_normalize_full_matches(FL_S("/.."), FL_S("/")));
    ASSERT_TRUE(path_normalize_full_matches(FL_S("/a/.."), FL_S("/")));
}

UTEST(path, normalize_full_keeps_unpoppable_dotdot_on_relative_paths) {
    ASSERT_TRUE(path_normalize_full_matches(FL_S("../../foo"), FL_S("../../foo")));
    ASSERT_TRUE(path_normalize_full_matches(FL_S("a/../../b"), FL_S("../b")));
    ASSERT_TRUE(path_normalize_full_matches(FL_S(".."), FL_S("..")));

    // ".." pops a real name and never a ".." already kept in the output.
    ASSERT_TRUE(path_normalize_full_matches(FL_S("../a/../.."), FL_S("../..")));
}

UTEST(path, normalize_full_relative_basics) {
    ASSERT_TRUE(path_normalize_full_matches(FL_S("a/./b"), FL_S("a/b")));
    ASSERT_TRUE(path_normalize_full_matches(FL_S("a/b/../c"), FL_S("a/c")));
    ASSERT_TRUE(path_normalize_full_matches(FL_S("."), FL_S(".")));
    ASSERT_TRUE(path_normalize_full_matches(FL_S("a/.."), FL_S(".")));
    ASSERT_TRUE(path_normalize_full_matches(FL_S(""), FL_S(".")));
}
