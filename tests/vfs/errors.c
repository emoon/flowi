// VFS read and listing failure tests

#include "vfs_test_support.h"

// A read-all of a file whose reported size exceeds the 512 MB cap fails before allocating the buffer and still
// closes the plugin file handle. It never returns truncated data as success.

UTEST(vfs, read_all_over_cap_reports_error_and_closes_handle) {
    FlVfsMount* mount = stub_mount_ready();
    ASSERT_TRUE_MSG(mount != nullptr, "stub mount should succeed");

    const LeakWatch leaks = leak_watch_begin(&s_stub_vfs_alloc_count, &s_stub_vfs_close_count);

    s_stub_vfs_oversize = true;
    FlVfsHandle handle = stub_read_all(mount);
    ASSERT_NE(handle, FL_VFS_HANDLE_INVALID);
    fl_vfs_wait(handle);
    s_stub_vfs_oversize = false;

    ASSERT_EQ_MSG(vfs_get_status(handle), (VfsOpStatus)VFS_ERROR_FILE_TOO_LARGE,
                  "over-cap read-all must report VFS_ERROR_FILE_TOO_LARGE");
    FlVfsData data = fl_vfs_get_data(handle);
    ASSERT_FALSE_MSG(data.success, "over-cap read-all must not report success");

    ASSERT_GT_MSG(leak_watch_allocated(&leaks), 0, "read-all should have opened at least one plugin handle");
    ASSERT_TRUE(leak_watch_balanced(&leaks));

    fl_vfs_close(handle);
    fl_vfs_mount_close(mount);
}

// A missing file, a failed size query and an over-cap file each end a read-all with their own error status.

static bool s_size_query_fails;

static FlVfsSizeResult maybe_failing_size(void* instance, void* handle, FlString relative_path) {
    if (s_size_query_fails) {
        return (FlVfsSizeResult) { .size = -1 };
    }
    return s_stub_vfs_plugin.get_size(instance, handle, relative_path);
}

static VfsOpStatus read_all_status(FlVfsMount* mount, FlString path) {
    FlVfsHandle handle
        = fl_vfs_mount_read_all_with_options(mount, path, nullptr, nullptr, nullptr, FL_VFS_HANDLE_INVALID);
    if (handle == FL_VFS_HANDLE_INVALID) {
        return VFS_STATE_SUCCESS;
    }
    fl_vfs_wait(handle);
    VfsOpStatus status = vfs_get_status(handle);
    fl_vfs_close(handle);
    return status;
}

typedef struct DistinctStatuses {
    VfsOpStatus missing;
    VfsOpStatus read_failure;
    VfsOpStatus over_cap;
} DistinctStatuses;

static DistinctStatuses read_distinct_statuses(FlVfsMount* mount) {
    DistinctStatuses statuses = { .missing = read_all_status(mount, FL_S("no_such_file.txt")) };

    s_size_query_fails = true;
    statuses.read_failure = read_all_status(mount, FL_S(STUB_VFS_FILE));
    s_size_query_fails = false;

    s_stub_vfs_oversize = true;
    statuses.over_cap = read_all_status(mount, FL_S(STUB_VFS_FILE));
    s_stub_vfs_oversize = false;
    return statuses;
}

static bool statuses_are_the_expected_ones(const DistinctStatuses* statuses) {
    return vfs_check(statuses->missing == (VfsOpStatus)VFS_ERROR_FILE_NOT_FOUND, "a missing file is not found")
           && vfs_check(statuses->read_failure == (VfsOpStatus)VFS_ERROR_READ, "a failed size query is a read error")
           && vfs_check(statuses->over_cap == (VfsOpStatus)VFS_ERROR_FILE_TOO_LARGE, "an over-cap file is too large");
}

static bool statuses_are_distinct(const DistinctStatuses* statuses) {
    return vfs_check(statuses->missing != statuses->read_failure, "missing differs from read failure")
           && vfs_check(statuses->missing != statuses->over_cap, "missing differs from over cap")
           && vfs_check(statuses->read_failure != statuses->over_cap, "read failure differs from over cap");
}

UTEST(vfs, missing_file_read_failure_and_over_cap_file_report_distinct_statuses) {
    ASSERT_TRUE(vfs_test_begin());
    static FlVfsPlugin wrapped;
    wrapped = s_stub_vfs_plugin;
    wrapped.get_size = maybe_failing_size;
    vfs_test_register_driver(&wrapped);

    FlVfsMount* mount = vfs_mount_ready(FL_S("/stub"));
    ASSERT_TRUE(mount != nullptr);

    const DistinctStatuses statuses = read_distinct_statuses(mount);
    ASSERT_TRUE(statuses_are_the_expected_ones(&statuses));
    ASSERT_TRUE(statuses_are_distinct(&statuses));

    fl_vfs_mount_close(mount);
}

// Many failures in a row do not exhaust error-string storage.

#define ERROR_LOOP_COUNT 6000
#define ERROR_LOOP_PATH_SIZE 240

// A path of ERROR_LOOP_PATH_SIZE characters, in the arena, that names nothing.
static FlString long_missing_path(void) {
    char* name = arena_alloc_array(s_arena, char, ERROR_LOOP_PATH_SIZE + 1);
    memset(name, 'x', ERROR_LOOP_PATH_SIZE);
    name[ERROR_LOOP_PATH_SIZE] = '\0';
    return string_from_cstr(name);
}

UTEST(vfs, error_strings_do_not_run_out_over_many_failures) {
    FlVfsMount* mount = stub_mount_ready();
    ASSERT_TRUE(mount != nullptr);

    FlString missing = long_missing_path();

    u64 error_bytes = 0;
    for_count(i, ERROR_LOOP_COUNT) {
        FlVfsHandle handle = vfs_mount_read_all(mount, missing);
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

    fl_vfs_mount_close(mount);
}

UTEST(vfs, failed_listings_carry_an_error_message_until_closed) {
    ASSERT_TRUE(vfs_test_begin());
    static FlVfsPlugin no_list;
    no_list = s_stub_vfs_plugin;
    no_list.list_directory = nullptr;
    vfs_test_register_driver(&no_list);

    FlVfsMount* mount = vfs_mount_ready(FL_S("/stub"));
    ASSERT_TRUE(mount != nullptr);

    FlString long_path = long_missing_path();

    u64 error_bytes = 0;
    for_count(i, ERROR_LOOP_COUNT) {
        FlVfsDirHandle handle = fl_vfs_get_listing(mount, long_path, 0);
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

    fl_vfs_mount_close(mount);
    vfs_destroy();
}

// A read-all callback owns whatever its user_data points at, so it is also called on failure, with no data, to
// let it release that. The case covers both places that notify: the error path inside the job
// (read_all_set_error, reached here through the over-cap branch) and the cancellation check before dispatch.

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

static bool failure_notified_once_without_data(const FailNotifyStats* stats) {
    return vfs_check(atomic_load(&stats->calls) == 1, "an in-job read failure must notify the callback exactly once")
           && vfs_check(atomic_load(&stats->calls_with_data) == 0, "a failure notification carries no data")
           && vfs_check(atomic_load(&stats->releases) == 0, "user_data stays until the failed read is closed");
}

UTEST(vfs, read_all_failure_notifies_the_callback) {
    FlVfsMount* mount = stub_mount_ready();
    ASSERT_TRUE_MSG(mount != nullptr, "stub mount should succeed");

    static FailNotifyStats over_cap;
    over_cap = (FailNotifyStats) { 0 };
    s_stub_vfs_oversize = true;
    FlVfsHandle handle = fl_vfs_mount_read_all_with_options(mount, FL_S(STUB_VFS_FILE), fail_notify_callback, &over_cap,
                                                            fail_notify_release, FL_VFS_HANDLE_INVALID);
    ASSERT_NE(handle, FL_VFS_HANDLE_INVALID);
    fl_vfs_wait(handle);
    s_stub_vfs_oversize = false;

    ASSERT_EQ_MSG(vfs_get_status(handle), (VfsOpStatus)VFS_ERROR_FILE_TOO_LARGE,
                  "over-cap read-all must report VFS_ERROR_FILE_TOO_LARGE");
    ASSERT_TRUE(failure_notified_once_without_data(&over_cap));
    fl_vfs_close(handle);
    ASSERT_EQ_MSG(atomic_load(&over_cap.releases), 1, "a failed read's close releases user_data exactly once");

    fl_vfs_mount_close(mount);
}
