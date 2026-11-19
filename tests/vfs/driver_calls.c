// VFS driver call tests: what a mount holds while a driver call runs

#include "vfs_test_support.h"

// One mutex per mount serializes the calls into its driver, so operations from several workers on the same mount
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
    if (parent_handle == nullptr) {
        if (!fl_string_equals(path, FL_S("serial"))) {
            return (FlVfsOpenResult) { .handle = nullptr, .error = stub_vfs_not_found() };
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

#define SERIAL_THREADS 4
#define SERIAL_READS_PER_THREAD 8
#define SERIAL_READ_COUNT (SERIAL_THREADS * SERIAL_READS_PER_THREAD)

// The unit_tests job system has a single worker, so the operations are run by hand from several threads, the way
// separate workers would run them.
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

// Everything the serial test runs by hand: read-alls that are prepared but not dispatched, and a listing and a file
// read per thread, so nothing runs until the threads execute them.
typedef struct SerialOps {
    FlVfsHandle reads[SERIAL_READ_COUNT];
    SerialRunCtx ctx[SERIAL_THREADS];
    VfsOp lists[SERIAL_THREADS];
    VfsOp file_reads[SERIAL_THREADS];
    u8 buffers[SERIAL_THREADS][sizeof(k_stub_vfs_data)];
} SerialOps;

static bool serial_prepare_reads(SerialOps* ops, FlVfsMount* mount) {
    for (int i = 0; i < SERIAL_READ_COUNT; i++) {
        ops->reads[i] = vfs_mount_read_all_prepare(mount, FL_S(STUB_VFS_FILE), nullptr, nullptr, nullptr,
                                                   FL_VFS_HANDLE_INVALID);
        if (ops->reads[i] == FL_VFS_HANDLE_INVALID) {
            return false;
        }
        SerialRunCtx* owner = &ops->ctx[i % SERIAL_THREADS];
        owner->reads[owner->read_count++] = vfs_op_acquire(ops->reads[i]);
    }
    return true;
}

static void serial_prepare_chained(SerialOps* ops, FlVfsMount* mount, FlVfsHandle file) {
    for (int i = 0; i < SERIAL_THREADS; i++) {
        ops->lists[i].mount = mount;
        ops->lists[i].op_type = VfsOp_MountList;
        ops->lists[i].path = FL_S("");
        ops->file_reads[i].mount = mount;
        ops->file_reads[i].op_type = VfsOp_FileRead;
        ops->file_reads[i].file_op = vfs_op_acquire(file); // a read owns its file reference and hands it back when run
        ops->file_reads[i].read_buffer = ops->buffers[i];
        ops->file_reads[i].read_size = (i64)sizeof(ops->buffers[i]);
        ops->ctx[i].chained[ops->ctx[i].chained_count++] = &ops->lists[i];
        ops->ctx[i].chained[ops->ctx[i].chained_count++] = &ops->file_reads[i];
    }
}

static bool serial_run_on_threads(SerialOps* ops) {
    Thread threads[SERIAL_THREADS];
    int started = 0;
    while (started < SERIAL_THREADS && os_thread_create(&threads[started], serial_run_thread, &ops->ctx[started])) {
        started++;
    }
    for (int i = 0; i < started; i++) {
        os_thread_join(&threads[i]);
    }
    return started == SERIAL_THREADS;
}

// Every read-all returned the whole file, and gives back the reference taken to run it by hand.
static bool serial_read_alls_succeeded(SerialOps* ops) {
    bool ok = true;
    for (int i = 0; i < SERIAL_READ_COUNT; i++) {
        FlVfsData data = fl_vfs_get_data(ops->reads[i]);
        ok = ok && vfs_check(data.success && (size_t)data.size == sizeof(k_stub_vfs_data), "a read-all of the file");
        vfs_op_release(ops->ctx[i % SERIAL_THREADS].reads[i / SERIAL_THREADS]);
        fl_vfs_close(ops->reads[i]);
    }
    return ok;
}

static bool serial_hand_built_ops_succeeded(SerialOps* ops) {
    bool ok = true;
    for (int i = 0; i < SERIAL_THREADS; i++) {
        ok = ok && vfs_check(atomic_load(&ops->lists[i].error_status) == VFS_STATE_SUCCESS, "a hand-built listing");
        fl_arena_destroy(ops->lists[i].result_arena);
        ok = ok && vfs_check(atomic_load(&ops->file_reads[i].error_status) == VFS_STATE_SUCCESS, "a file read");
        mi_free(atomic_load(&ops->file_reads[i].result_data));
    }
    return ok;
}

// Every read-all and every hand-built file read went through the driver once, and no two calls overlapped.
static bool serial_driver_saw_one_call_at_a_time(void) {
    return vfs_check(atomic_load(&s_serial_reads) == SERIAL_READ_COUNT + SERIAL_THREADS, "driver reads")
           && vfs_check(atomic_load(&s_serial_lists) == SERIAL_THREADS, "driver listings")
           && vfs_check(atomic_load(&s_serial_max_in_flight) == 1,
                        "two threads were inside the same mount's driver at once");
}

UTEST(vfs, operations_on_one_mount_never_overlap_inside_its_driver) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_serial_vfs_plugin);
    atomic_store(&s_serial_in_flight, 0);
    atomic_store(&s_serial_max_in_flight, 0);
    atomic_store(&s_serial_reads, 0);
    atomic_store(&s_serial_lists, 0);

    FlVfsMount* mount = vfs_mount_ready(FL_S("serial"));
    ASSERT_TRUE(mount != nullptr);

    FlVfsHandle file = fl_vfs_mount_open(mount, FL_S(STUB_VFS_FILE), FlVfsOpenFlags_Read);
    fl_vfs_wait(file);

    static SerialOps ops;
    memset(&ops, 0, sizeof(ops));
    ASSERT_TRUE(serial_prepare_reads(&ops, mount));
    serial_prepare_chained(&ops, mount, file);

    ASSERT_TRUE(serial_run_on_threads(&ops));
    ASSERT_TRUE(serial_read_alls_succeeded(&ops));
    ASSERT_TRUE(serial_hand_built_ops_succeeded(&ops));
    ASSERT_TRUE(serial_driver_saw_one_call_at_a_time());

    fl_vfs_close(file);
    fl_vfs_mount_close(mount);
}

// Mounting from a worker thread runs the driver without holding any lock the rest of the VFS needs. While one
// thread is parked inside a driver's open, another mount starts and finishes.

static _Atomic bool s_gate_entered;
static _Atomic bool s_gate_release;

static FlVfsOpenResult gated_open(void* instance, void* parent_handle, FlString path, uint32_t flags,
                                  FlVfsMountOptions mount_options) {
    if (parent_handle != nullptr) {
        return stub_vfs_open(instance, parent_handle, path, flags, mount_options);
    }
    if (!fl_string_equals(path, FL_S("gated"))) {
        return (FlVfsOpenResult) { .handle = nullptr, .error = stub_vfs_not_found() };
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

// Runs as a job, so the mount below is resolved on the worker itself and not scheduled.
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

static void gate_reset(void) {
    atomic_store(&s_gate_entered, false);
    atomic_store(&s_gate_release, false);
}

static void gated_ctx_init(GatedMountCtx* ctx, const char* source) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->source = string_from_cstr(source);
    atomic_init(&ctx->done, false);
}

// Mounts "/stub" from a plain thread while the gated mount's driver call is in flight. Returns whether that mount
// finished, and releases the gate either way.
static bool other_mount_finishes_while_gated(GatedMountCtx* other, bool* thread_created) {
    Thread other_thread;
    *thread_created = os_thread_create(&other_thread, gated_other_mount_thread, other);
    const bool finished = *thread_created && vfs_await_flag_for(&other->done, 3000000000ull);

    atomic_store(&s_gate_release, true);
    if (*thread_created) {
        os_thread_join(&other_thread);
    }
    return finished;
}

UTEST(vfs, mounting_from_a_worker_holds_no_global_lock_while_the_driver_runs) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);
    vfs_test_register_driver(&s_gated_vfs_plugin);
    gate_reset();

    static GatedMountCtx gated;
    static GatedMountCtx other;
    gated_ctx_init(&gated, "gated");
    gated_ctx_init(&other, "/stub");

    FlJobHandle gated_job = fl_jobs_add_job(gated_mount_job, &gated);
    const bool gate_entered = vfs_await_flag_for(&s_gate_entered, VFS_AWAIT_SHORT_NS);
    if (!gate_entered) {
        atomic_store(&s_gate_release, true);
        fl_jobs_wait(gated_job);
    }
    ASSERT_TRUE_MSG(gate_entered, "the gated mount never reached its driver");

    bool thread_created = false;
    const bool other_started = other_mount_finishes_while_gated(&other, &thread_created);
    fl_jobs_wait(gated_job);
    ASSERT_TRUE(thread_created);

    ASSERT_TRUE_MSG(other_started, "a mount was blocked while another mount's driver call was in flight");

    vfs_wait_all();
    ASSERT_TRUE(fl_vfs_mount_is_ready(gated.mount));
    ASSERT_TRUE(fl_vfs_mount_is_ready(other.mount));
    fl_vfs_mount_close(gated.mount);
    fl_vfs_mount_close(other.mount);
}

// A mount reports pending while its job is inside the driver, then ready once the job publishes.

UTEST(vfs, mount_reports_pending_until_its_job_publishes_ready) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_gated_vfs_plugin);
    gate_reset();

    FlVfsMountResult mount = vfs_mount(FL_S("gated"));
    ASSERT_EQ(mount.status, FlVfsMountErrorStatus_Success);
    const bool entered = vfs_await_flag_for(&s_gate_entered, VFS_AWAIT_SHORT_NS);
    const int status_while_gated = (int)fl_vfs_mount_get_status(mount.mount);

    atomic_store(&s_gate_release, true);
    vfs_wait_all();
    ASSERT_TRUE_MSG(entered, "the mount job never reached the driver");
    ASSERT_EQ(status_while_gated, (int)FlVfsMountStatus_Pending);
    ASSERT_EQ((int)fl_vfs_mount_get_status(mount.mount), (int)FlVfsMountStatus_Ready);

    fl_vfs_mount_close(mount.mount);
}

// fl_vfs_close never waits for the mount's driver. A worker parked inside a read holds the driver lock, and
// closing a different file of the same mount still returns at once.

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
    FlVfsMount* mount = stub_mount_started();
    ASSERT_TRUE(mount != nullptr);

    FlVfsHandle busy = fl_vfs_mount_open(mount, FL_S(STUB_VFS_FILE), FlVfsOpenFlags_Read);
    FlVfsHandle idle = fl_vfs_mount_open(mount, FL_S(STUB_VFS_FILE), FlVfsOpenFlags_Read);
    fl_vfs_wait(busy);
    fl_vfs_wait(idle);

    u8 buffer[sizeof(k_stub_vfs_data)];
    stub_read_gate_arm();
    FlVfsHandle read = fl_vfs_read(busy, buffer, sizeof(buffer));
    const bool parked = vfs_await_flag_for(&s_stub_vfs_read_entered, VFS_AWAIT_SHORT_NS);

    CloseDuringReadCtx ctx = { .file = idle };
    atomic_init(&ctx.done, false);
    Thread closer;
    const bool started = os_thread_create(&closer, close_during_read_thread, &ctx);
    const bool closed_while_parked = started && vfs_await_flag_for(&ctx.done, 3000000000ull);

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
    fl_vfs_mount_close(mount);
}
