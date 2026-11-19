// VFS tests for calls made from a job worker, which cannot wait: closes, chained reads and dispatches that race
// the operations the worker itself is running

#include "vfs_test_support.h"

// fl_jobs_wait() does nothing on a job worker, so fl_vfs_wait() cannot wait for a running job from one.
// fl_vfs_close() needs no wait: it only gives up the id, and the running job keeps the op alive until it ends. A
// read-all callback runs on the worker inside the read's own job, so a close issued there is a close of an
// in-flight handle.

typedef struct VfsWorkerCloseCtx {
    FlVfsHandle handle;
    _Atomic bool ready_during_wait;
    _Atomic bool close_attempted;
    _Atomic int releases;
    _Atomic bool released_after_close;
} VfsWorkerCloseCtx;

static FlVfsData vfs_worker_close_callback(void* data, i64 size, void* user_data) {
    VfsWorkerCloseCtx* ctx = (VfsWorkerCloseCtx*)user_data;

    // fl_vfs_wait() cannot block a worker either, and its guard only logs. The flag below marks that the close
    // happened while the job was in flight.
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

static void worker_close_ctx_init(VfsWorkerCloseCtx* ctx) {
    memset(ctx, 0, sizeof(*ctx));
    atomic_init(&ctx->ready_during_wait, true);
    atomic_init(&ctx->close_attempted, false);
    atomic_init(&ctx->releases, 0);
    atomic_init(&ctx->released_after_close, false);
}

static bool worker_close_released(const void* arg) {
    return atomic_load(&((const VfsWorkerCloseCtx*)arg)->releases) != 0;
}

static bool worker_close_took_effect(const VfsWorkerCloseCtx* ctx) {
    return vfs_check(atomic_load(&ctx->close_attempted), "the callback must have run and closed its own handle")
           && vfs_check(!atomic_load(&ctx->ready_during_wait),
                        "the callback must have run with its own read still in flight")
           && vfs_check(!fl_vfs_is_ready(ctx->handle), "a close from a worker must retire the id at once")
           && vfs_check(!fl_vfs_get_data(ctx->handle).success, "a retired id carries no data")
           && vfs_check(atomic_load(&ctx->releases) == 1, "the hook runs once, when the job ends")
           && vfs_check(atomic_load(&ctx->released_after_close),
                        "the hook must not run before the job's callback is done");
}

UTEST(vfs, close_from_worker_takes_effect_and_the_running_job_still_finishes) {
    FlVfsMount* mount = stub_mount_ready();
    ASSERT_TRUE_MSG(mount != nullptr, "stub mount should succeed");

    static VfsWorkerCloseCtx ctx;
    worker_close_ctx_init(&ctx);
    ctx.handle = vfs_mount_read_all_prepare(mount, FL_S(STUB_VFS_FILE), vfs_worker_close_callback, &ctx,
                                            vfs_worker_close_release, FL_VFS_HANDLE_INVALID);
    ASSERT_NE(ctx.handle, FL_VFS_HANDLE_INVALID);
    vfs_dispatch(ctx.handle);

    // The close retires the id, so fl_vfs_wait(id) does nothing here. The release hook reports that the job is
    // over.
    (void)vfs_await_until(worker_close_released, &ctx, VFS_AWAIT_SHORT_NS);

    ASSERT_TRUE(worker_close_took_effect(&ctx));

    fl_vfs_close(ctx.handle); // a stale id, so nothing happens
    vfs_wait_all();
    ASSERT_EQ_MSG(atomic_load(&ctx.releases), 1, "a second close and a wait_all must not release again");

    fl_vfs_mount_close(mount);
}

// A read issued from a worker against a mount that is still coming up is scheduled behind that mount's own job,
// so the worker's wait cannot make it ready. Its close from the worker still takes effect, and the read still
// runs once the mount is up. unit_tests runs a one-worker job system, and parking that worker in a read-all
// callback keeps the second mount below pending.

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

// Runs on the parked worker: reads from the mount that is still coming up, waits on the read and closes it.
static void vfs_pending_mount_probe(VfsPendingMountCtx* ctx, FlVfsMount* mount) {
    atomic_store(&ctx->mount_pending_at_launch, !fl_vfs_mount_is_ready(mount));

    FlVfsHandle ticket = fl_vfs_mount_read_all_with_options(mount, FL_S(STUB_VFS_FILE), vfs_pending_ticket_callback, ctx,
                                                            vfs_pending_ticket_release, FL_VFS_HANDLE_INVALID);
    atomic_store(&ctx->ticket, ticket);

    fl_vfs_wait(ticket);
    atomic_store(&ctx->ready_after_wait, fl_vfs_is_ready(ticket));
    fl_vfs_close(ticket);
}

static FlVfsData vfs_pending_mount_callback(void* data, i64 size, void* user_data) {
    VfsPendingMountCtx* ctx = (VfsPendingMountCtx*)user_data;
    atomic_store(&ctx->in_callback, true);

    (void)vfs_await_flag(&ctx->setup_done);

    FlVfsMount* mount = atomic_load(&ctx->mount);
    if (mount != nullptr) {
        vfs_pending_mount_probe(ctx, mount);
    }

    atomic_store(&ctx->probe_done, true);
    (void)vfs_await_flag(&ctx->release);

    return (FlVfsData) { .data = data, .size = size, .success = true };
}

static void pending_ctx_init(VfsPendingMountCtx* ctx) {
    memset(ctx, 0, sizeof(*ctx));
    atomic_init(&ctx->mount, nullptr);
    atomic_init(&ctx->in_callback, false);
    atomic_init(&ctx->setup_done, false);
    atomic_init(&ctx->ticket, FL_VFS_HANDLE_INVALID);
    atomic_init(&ctx->mount_pending_at_launch, false);
    atomic_init(&ctx->ready_after_wait, true);
    atomic_init(&ctx->probe_done, false);
    atomic_init(&ctx->release, false);
    atomic_init(&ctx->ticket_callbacks, 0);
    atomic_init(&ctx->ticket_releases, 0);
    atomic_init(&ctx->ticket_bytes_matched, false);
    atomic_init(&ctx->ticket_released_before_callback, false);
}

static bool pending_probe_saw_a_pending_mount(const VfsPendingMountCtx* ctx, bool callback_started,
                                              bool probe_finished) {
    return vfs_check(callback_started, "the read-all callback never ran")
           && vfs_check(probe_finished, "the worker never finished reading from the pending mount")
           && vfs_check(atomic_load(&ctx->mount_pending_at_launch),
                        "the second mount must still have been coming up when the worker read from it")
           && vfs_check(!atomic_load(&ctx->ready_after_wait),
                        "a read deferred behind a pending mount must not be ready after a worker's no-op wait");
}

// The close took effect on the worker, and the read it left behind still ran once the mount was up: its callback
// once, then its release hook once.
static bool pending_ticket_ran_after_its_close(const VfsPendingMountCtx* ctx) {
    return vfs_check(!fl_vfs_is_ready(atomic_load(&ctx->ticket)), "the worker's close must have retired the ticket")
           && vfs_check(atomic_load(&ctx->ticket_callbacks) == 1, "the deferred read must run once the mount is up")
           && vfs_check(atomic_load(&ctx->ticket_releases) == 1,
                        "the closed read must release its user_data once it ran")
           && vfs_check(atomic_load(&ctx->ticket_bytes_matched), "the deferred read must deliver the file's bytes")
           && vfs_check(!atomic_load(&ctx->ticket_released_before_callback),
                        "the release must follow the callback");
}

UTEST(vfs, worker_read_against_a_pending_mount_is_deferred_and_still_runs_after_its_close) {
    FlVfsMount* ready_mount = stub_mount_ready();
    ASSERT_TRUE_MSG(ready_mount != nullptr, "stub mount should succeed");

    static VfsPendingMountCtx ctx;
    pending_ctx_init(&ctx);

    FlVfsHandle parker = vfs_mount_read_all_prepare(ready_mount, FL_S(STUB_VFS_FILE), vfs_pending_mount_callback, &ctx,
                                                    nullptr, FL_VFS_HANDLE_INVALID);
    ASSERT_NE(parker, FL_VFS_HANDLE_INVALID);
    vfs_dispatch(parker);

    // Every wait below has a deadline and does not assert, and the release is unconditional. From here on the only
    // worker is parked in the callback. An ASSERT returns from the case without releasing it, which would leave
    // vfs_destroy waiting on a spinning worker.
    const bool callback_started = vfs_await_flag_for(&ctx.in_callback, VFS_AWAIT_SHORT_NS);

    FlVfsMountResult pending_mount = vfs_mount(FL_S("/stub"));
    atomic_store(&ctx.mount, pending_mount.status == FlVfsMountErrorStatus_Success ? pending_mount.mount : nullptr);
    atomic_store(&ctx.setup_done, true);

    const bool probe_finished = vfs_await_flag_for(&ctx.probe_done, 2 * VFS_AWAIT_SHORT_NS);
    atomic_store(&ctx.release, true);

    fl_vfs_wait(parker);
    vfs_wait_all(); // lets the second mount come up and the read deferred behind it run

    ASSERT_TRUE(pending_probe_saw_a_pending_mount(&ctx, callback_started, probe_finished));
    ASSERT_EQ_MSG(pending_mount.status, FlVfsMountErrorStatus_Success, "second stub mount should succeed");
    ASSERT_NE(atomic_load(&ctx.ticket), FL_VFS_HANDLE_INVALID);
    ASSERT_TRUE(pending_ticket_ran_after_its_close(&ctx));

    fl_vfs_close(parker);
    fl_vfs_mount_close(pending_mount.mount);
    fl_vfs_mount_close(ready_mount);
}

// A worker runs a chained read inline because it cannot wait for the op it chains onto. Against an open that has
// not run yet, that would read a file handle whose plugin_file_handle is still null ("File not opened"), so the
// chain is refused. unit_tests runs a one-worker job system, and parking that worker in a read-all callback
// keeps the open below queued for as long as the callback holds it.

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

static void worker_chain_ctx_init(VfsWorkerChainCtx* ctx) {
    memset(ctx, 0, sizeof(*ctx));
    atomic_init(&ctx->file, FL_VFS_HANDLE_INVALID);
    atomic_init(&ctx->file_ready, false);
    atomic_init(&ctx->in_callback, false);
    atomic_init(&ctx->chained_status, VFS_STATE_PENDING);
    atomic_init(&ctx->chain_done, false);
}

typedef struct WorkerChainFindings {
    bool parked;
    bool still_queued;
    bool chained;
} WorkerChainFindings;

static bool worker_chain_was_refused(const VfsWorkerChainCtx* ctx, const WorkerChainFindings* found) {
    return vfs_check(found->parked, "the read-all callback must park the only worker")
           && vfs_check(found->still_queued, "the open must still be queued behind the parked worker")
           && vfs_check(found->chained, "the parked worker never attempted the chained read")
           && vfs_check(atomic_load(&ctx->chained_status) == (VfsOpStatus)VFS_ERROR_WOULD_BLOCK,
                        "chaining onto an unfinished open from a worker must refuse, not read an unopened file");
}

// The refusal left the open untouched: it still completes and still serves a read from the main thread.
static bool refused_open_still_reads(FlVfsHandle file) {
    u8 buffer[sizeof(k_stub_vfs_data)] = { 0 };
    FlVfsHandle read = fl_vfs_read(file, buffer, sizeof(buffer));
    if (read == FL_VFS_HANDLE_INVALID) {
        return false;
    }
    fl_vfs_wait(read);
    FlVfsData data = fl_vfs_get_data(read);
    const bool ok = vfs_check(data.success, "the open a refused chain left alone must still read")
                    && vfs_check(memcmp(buffer, k_stub_vfs_data, sizeof(k_stub_vfs_data)) == 0, "the bytes");
    fl_vfs_close(read);
    return ok;
}

UTEST(vfs, chained_read_from_worker_refuses_unfinished_open) {
    FlVfsMount* mount = stub_mount_ready();
    ASSERT_TRUE_MSG(mount != nullptr, "stub mount should succeed");

    static VfsWorkerChainCtx ctx;
    worker_chain_ctx_init(&ctx);

    FlVfsHandle reader = vfs_mount_read_all_prepare(mount, FL_S(STUB_VFS_FILE), vfs_worker_chain_callback, &ctx,
                                                    nullptr, FL_VFS_HANDLE_INVALID);
    ASSERT_NE(reader, FL_VFS_HANDLE_INVALID);
    vfs_dispatch(reader);

    // The worker is parked until ctx.file is stored, so nothing may ASSERT before then. Findings go into locals.
    WorkerChainFindings found = { .parked = vfs_await_flag(&ctx.in_callback) };

    FlVfsHandle file = fl_vfs_mount_open(mount, FL_S(STUB_VFS_FILE), FlVfsOpenFlags_Read);
    found.still_queued = file != FL_VFS_HANDLE_INVALID && !fl_vfs_is_ready(file);
    atomic_store(&ctx.file, file);
    atomic_store(&ctx.file_ready, true);

    found.chained = vfs_await_flag(&ctx.chain_done);

    ASSERT_NE(file, FL_VFS_HANDLE_INVALID);
    ASSERT_TRUE(worker_chain_was_refused(&ctx, &found));

    fl_vfs_wait(reader);
    ASSERT_TRUE(fl_vfs_get_data(reader).success);
    fl_vfs_close(reader);

    ASSERT_TRUE(refused_open_still_reads(file));

    fl_vfs_close(file);
    fl_vfs_mount_close(mount);
}

// A read chained onto a file holds a reference to it from creation until it has run. The case closes the file in
// that span: the worker parks in a read-all callback and chains a read onto an open that has already finished,
// the gated stub read holds the read open, and the main thread closes the file handle meanwhile. The close gives
// up the id but does not free the file or the plugin_file_handle the blocked read is using.

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

static void chain_race_ctx_init(VfsChainRaceCtx* ctx, FlVfsHandle file) {
    memset(ctx, 0, sizeof(*ctx));
    atomic_init(&ctx->in_callback, false);
    atomic_init(&ctx->start_chain, false);
    atomic_init(&ctx->release, false);
    atomic_init(&ctx->file, file);
    atomic_init(&ctx->chained_status, (int)VFS_STATE_PENDING);
    atomic_init(&ctx->chained_matched, false);
    atomic_init(&ctx->chain_done, false);
}

typedef struct ChainRaceFindings {
    bool parked;
    bool in_window;
    bool chained;
    int closes_during;
    int closes_total;
} ChainRaceFindings;

// From here the worker is parked, so nothing may ASSERT until it is released below. An ASSERT returns from the case
// and leaves teardown waiting on a worker that never comes back. Findings go into the result.
static ChainRaceFindings chain_race_run(VfsChainRaceCtx* ctx, FlVfsHandle file) {
    ChainRaceFindings found = { .parked = vfs_await_flag(&ctx->in_callback) };

    // The gate is armed only now, after the read-all's own read of the file has gone through stub_vfs_read.
    stub_read_gate_arm();
    atomic_store(&ctx->start_chain, true);

    found.in_window = found.parked && vfs_await_flag(&s_stub_vfs_read_entered);

    // The worker is now blocked in the driver's read, through the file the main thread is about to close.
    const int closes_before = s_stub_vfs_close_count;
    if (found.in_window) {
        fl_vfs_close(file);
    }
    found.closes_during = s_stub_vfs_close_count - closes_before;

    atomic_store(&s_stub_vfs_read_release, true);
    found.chained = found.in_window && vfs_await_flag(&ctx->chain_done);
    atomic_store(&s_stub_vfs_read_gate_armed, false);
    atomic_store(&ctx->release, true);
    vfs_wait_all();
    found.closes_total = s_stub_vfs_close_count - closes_before;
    return found;
}

static bool chain_race_reached_the_window(const ChainRaceFindings* found) {
    return vfs_check(found->parked, "the read-all callback must park the only worker")
           && vfs_check(found->in_window, "the chained read must reach the driver and hold the scheduling window open")
           && vfs_check(found->chained, "the chained read must complete once the gate is released");
}

static bool chain_race_close_was_deferred(const VfsChainRaceCtx* ctx, const ChainRaceFindings* found) {
    return vfs_check(found->closes_during == 0,
                     "a close landing while a chained read runs must defer: freeing there closes the plugin file "
                     "handle the chained read is still reading through")
           && vfs_check(atomic_load(&ctx->chained_status) == (int)VFS_STATE_SUCCESS,
                        "the read chained across the close must still complete against a live file handle")
           && vfs_check(atomic_load(&ctx->chained_matched), "the chained read must deliver the file's bytes")
           // The close was deferred and not cancelled: the read hands the file back when it ends, which frees the
           // file and the driver's file handle exactly once.
           && vfs_check(found->closes_total == 1,
                        "the deferred close must still release the plugin file handle exactly once");
}

UTEST(vfs, close_racing_a_chained_read_defers_instead_of_freeing_the_file_handle) {
    FlVfsMount* mount = stub_mount_started();
    ASSERT_TRUE_MSG(mount != nullptr, "stub mount should succeed");

    // The open has to finish before the worker parks. A chain onto an unfinished open is refused, and a finished
    // predecessor is what makes the handle look idle to a close that does not check for chained reads.
    FlVfsHandle file = fl_vfs_mount_open(mount, FL_S(STUB_VFS_FILE), FlVfsOpenFlags_Read);
    ASSERT_NE(file, FL_VFS_HANDLE_INVALID);
    fl_vfs_wait(file);
    ASSERT_TRUE(fl_vfs_is_ready(file));

    static VfsChainRaceCtx ctx;
    chain_race_ctx_init(&ctx, file);

    FlVfsHandle parker = vfs_mount_read_all_prepare(mount, FL_S(STUB_VFS_FILE), vfs_chain_race_callback, &ctx, nullptr,
                                                    FL_VFS_HANDLE_INVALID);
    ASSERT_NE(parker, FL_VFS_HANDLE_INVALID);
    vfs_dispatch(parker);

    const ChainRaceFindings found = chain_race_run(&ctx, file);

    ASSERT_TRUE(chain_race_reached_the_window(&found));
    ASSERT_TRUE(chain_race_close_was_deferred(&ctx, &found));

    fl_vfs_mount_close(mount);
}

// An op whose dispatch has not returned yet has no job handle recorded, so a mount close has nothing to wait on
// for it. The job's own reference keeps the close from freeing it. To reach that window, the only worker parks
// in a read-all on a second mount and dispatches a read-all on the mount under test from there. That one runs
// inline and blocks in the gated stub read while the dispatch is still unrecorded. While the job is reading
// through the op, the close neither frees the op (or runs its release hook) nor returns.

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

// Runs on the only worker. Scheduling from there runs the dispatched job inline, so vfs_dispatch stays in its
// unlocked window for as long as the gated stub read holds that job.
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

static bool dispatch_race_freed_in_flight(const void* arg) {
    return atomic_load(&((const VfsDispatchRaceCtx*)arg)->freed_in_flight);
}

// Lets the gated read go once the close has had its chance to free the victim early. The main thread cannot do
// it, because its fl_vfs_mount_close is waiting for this release.
static void* vfs_dispatch_race_releaser(void* user_data) {
    VfsDispatchRaceCtx* ctx = (VfsDispatchRaceCtx*)user_data;

    if (vfs_await_flag(&ctx->close_started)) {
        (void)vfs_await_until(dispatch_race_freed_in_flight, ctx, 100000000ull);
    }

    atomic_store(&s_stub_vfs_read_release, true);
    return nullptr;
}

static void dispatch_race_ctx_init(VfsDispatchRaceCtx* ctx, FlVfsMount* target) {
    *ctx = (VfsDispatchRaceCtx) { .target = target };
    atomic_init(&ctx->in_callback, false);
    atomic_init(&ctx->start_dispatch, false);
    atomic_init(&ctx->dispatch_returned, false);
    atomic_init(&ctx->close_started, false);
    atomic_init(&ctx->freed_in_flight, false);
    atomic_init(&ctx->victim_released, false);
    atomic_init(&ctx->victim_callback_calls, 0);
    atomic_init(&ctx->victim_bytes_matched, false);
    atomic_init(&ctx->release_parker, false);
}

typedef struct DispatchRaceFindings {
    bool parked;
    bool in_window;
    bool releasing;
    bool dispatch_returned;
    bool drained;
} DispatchRaceFindings;

// Starts the releaser and runs the close under test. The main-thread close waits for the victim's job and its hook
// before it returns.
static void dispatch_race_close_target(VfsDispatchRaceCtx* ctx, DispatchRaceFindings* found, Thread* releaser) {
    found->releasing = found->in_window && os_thread_create(releaser, vfs_dispatch_race_releaser, ctx);
    if (!found->releasing) {
        return;
    }
    atomic_store(&ctx->close_started, true);
    fl_vfs_mount_close(ctx->target);
    found->drained = atomic_load(&ctx->victim_released) && atomic_load(&ctx->victim_callback_calls) == 1;
    found->dispatch_returned = vfs_await_flag(&ctx->dispatch_returned);
}

// From here the only worker is parked, so nothing may ASSERT until it is released below. An ASSERT returns from the
// case and leaves teardown waiting on a worker that never comes back. Findings go into the result.
static DispatchRaceFindings dispatch_race_run(VfsDispatchRaceCtx* ctx, FlVfsHandle parker, FlVfsMount* host) {
    DispatchRaceFindings found = { .parked = vfs_await_flag(&ctx->in_callback) };

    // The gate is armed only now, after the parker's own read of the file has gone through stub_vfs_read.
    stub_read_gate_arm();
    atomic_store(&ctx->start_dispatch, true);

    found.in_window = found.parked && vfs_await_flag(&s_stub_vfs_read_entered);

    Thread releaser = { 0 };
    dispatch_race_close_target(ctx, &found, &releaser);

    atomic_store(&s_stub_vfs_read_release, true);
    atomic_store(&s_stub_vfs_read_gate_armed, false);
    atomic_store(&ctx->release_parker, true);
    if (found.releasing) {
        os_thread_join(&releaser);
    }
    fl_vfs_wait(parker);
    fl_vfs_close(parker);
    if (!found.releasing) {
        fl_vfs_mount_close(ctx->target); // the case bailed before the close under test could run it
    }
    fl_vfs_mount_close(host);
    return found;
}

static bool dispatch_race_reached_the_window(const DispatchRaceFindings* found) {
    return vfs_check(found->parked, "the read-all callback must park the only worker")
           && vfs_check(found->in_window,
                        "the dispatched read must reach the driver and hold the scheduling window open")
           && vfs_check(found->releasing, "the releaser thread must start; the close under test blocks without it")
           && vfs_check(found->dispatch_returned, "the dispatch must record its job and return");
}

static bool dispatch_race_close_waited(const VfsDispatchRaceCtx* ctx, const DispatchRaceFindings* found) {
    return vfs_check(found->drained,
                     "fl_vfs_mount_close on the main thread must not return before the victim has run and released")
           && vfs_check(!atomic_load(&ctx->freed_in_flight),
                        "fl_vfs_mount_close must not free a victim whose dispatch has not recorded its job yet: the "
                        "job is still running against the handle")
           && vfs_check(atomic_load(&ctx->victim_callback_calls) == 1,
                        "the dispatched read must run to completion against a live handle")
           && vfs_check(atomic_load(&ctx->victim_bytes_matched), "the dispatched read must deliver the file's bytes");
}

UTEST(vfs, mount_close_waits_out_a_dispatch_that_has_not_recorded_its_job) {
    stub_vfs_begin();

    // Two mounts: the close under test tears down target, while the parked worker's own read-all is on host,
    // which that close neither drains nor frees.
    FlVfsMount* target = vfs_mount_started(FL_S("/stub"));
    ASSERT_TRUE_MSG(target != nullptr, "stub mount should succeed");
    FlVfsMount* host = vfs_mount_started(FL_S("/stub"));
    ASSERT_TRUE_MSG(host != nullptr, "stub mount should succeed");
    ASSERT_TRUE(fl_vfs_mount_is_ready(target));
    ASSERT_TRUE(fl_vfs_mount_is_ready(host));

    VfsDispatchRaceCtx ctx;
    dispatch_race_ctx_init(&ctx, target);

    FlVfsHandle parker = vfs_mount_read_all_prepare(host, FL_S(STUB_VFS_FILE), vfs_dispatch_race_callback, &ctx, nullptr,
                                                    FL_VFS_HANDLE_INVALID);
    ASSERT_NE(parker, FL_VFS_HANDLE_INVALID);
    vfs_dispatch(parker);

    const DispatchRaceFindings found = dispatch_race_run(&ctx, parker, host);

    ASSERT_TRUE(dispatch_race_reached_the_window(&found));
    ASSERT_TRUE(dispatch_race_close_waited(&ctx, &found));
}

// A worker cannot wait, so a mount close from one returns at once without draining. It refuses new operations on
// the mount, and the mount is freed and its root handle closed when the last operation holding it ends.

typedef struct VfsWorkerMountCloseCtx {
    FlVfsMount* mount;
    LeakWatch leaks;
    _Atomic FlVfsHandle late;
    _Atomic bool closed_mount;
    _Atomic int open_handles_after_close;
} VfsWorkerMountCloseCtx;

static FlVfsData vfs_worker_mount_close_callback(void* data, i64 size, void* user_data) {
    VfsWorkerMountCloseCtx* ctx = (VfsWorkerMountCloseCtx*)user_data;

    fl_vfs_mount_close(ctx->mount);
    // The reader's own job still holds the mount, so its root handle is the one thing left open.
    atomic_store(&ctx->open_handles_after_close, leak_watch_allocated(&ctx->leaks) - leak_watch_closed(&ctx->leaks));
    atomic_store(&ctx->late, stub_read_all(ctx->mount));
    atomic_store(&ctx->closed_mount, true);

    return (FlVfsData) { .data = data, .size = size, .success = true };
}

static bool worker_mount_close_is_freed(const void* arg) {
    const VfsWorkerMountCloseCtx* ctx = arg;
    return atomic_load(&ctx->closed_mount) && leak_watch_closed(&ctx->leaks) == leak_watch_allocated(&ctx->leaks);
}

static bool worker_mount_close_refused_and_freed(const VfsWorkerMountCloseCtx* ctx, bool freed, FlVfsHandle reader) {
    return vfs_check(atomic_load(&ctx->closed_mount), "the callback must have closed the mount from the worker")
           && vfs_check(atomic_load(&ctx->late) == FL_VFS_HANDLE_INVALID, "a closing mount must refuse new operations")
           && vfs_check(atomic_load(&ctx->open_handles_after_close) == 1,
                        "a mount closed from a worker must keep its root open while an operation still holds it")
           && vfs_check(leak_watch_allocated(&ctx->leaks) > 0, "the mount opened a root handle")
           && vfs_check(freed, "the mount's root handle must close once its last operation has ended")
           && vfs_check(!fl_vfs_is_ready(reader), "the mount close released the reader's id");
}

UTEST(vfs, mount_close_from_a_worker_refuses_new_ops_and_frees_on_the_last_reference) {
    stub_vfs_begin();

    const LeakWatch leaks = leak_watch_begin(&s_stub_vfs_alloc_count, &s_stub_vfs_close_count);

    FlVfsMount* mount = vfs_mount_ready(FL_S("/stub"));
    ASSERT_TRUE_MSG(mount != nullptr, "stub mount should succeed");

    VfsWorkerMountCloseCtx ctx = { .mount = mount, .leaks = leaks };
    atomic_init(&ctx.late, FL_VFS_HANDLE_INVALID + 1);
    atomic_init(&ctx.closed_mount, false);
    atomic_init(&ctx.open_handles_after_close, -1);
    FlVfsHandle reader = vfs_mount_read_all_prepare(mount, FL_S(STUB_VFS_FILE), vfs_worker_mount_close_callback, &ctx,
                                                    nullptr, FL_VFS_HANDLE_INVALID);
    ASSERT_NE(reader, FL_VFS_HANDLE_INVALID);
    vfs_dispatch(reader);

    // The reader's own job is the last thing holding the mount, and the mount's root closes when that job ends.
    const bool freed = vfs_await_until(worker_mount_close_is_freed, &ctx, VFS_AWAIT_SHORT_NS);

    ASSERT_TRUE(worker_mount_close_refused_and_freed(&ctx, freed, reader));
}

// A mount closed from a worker leaves the mount list only when it is freed, so vfs_wait_all still waits for the
// ops it has running. Reader A, on the only worker, closes the mount from its callback. B was queued behind it
// before the close and runs against a closing mount. wait_all returns after B has run and the root has closed.

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

static bool wait_all_covered_the_closed_mount(bool parked, bool closed, const VfsReleaseCtx* queued,
                                              const LeakWatch* leaks) {
    return vfs_check(parked, "the closing reader never ran") && vfs_check(closed, "the reader never closed the mount")
           && vfs_check(atomic_load(&queued->callback_calls) == 1,
                        "wait_all returned before the op queued on the closed mount ran")
           && vfs_check(atomic_load(&queued->release_calls) == 1, "the queued op released its user_data")
           && vfs_check(leak_watch_balanced(leaks), "wait_all returned before the closed mount had been freed");
}

UTEST(vfs, wait_all_covers_the_ops_of_a_mount_closed_from_a_worker) {
    stub_vfs_begin();

    const LeakWatch leaks = leak_watch_begin(&s_stub_vfs_alloc_count, &s_stub_vfs_close_count);

    FlVfsMount* mount = vfs_mount_started(FL_S("/stub"));
    ASSERT_TRUE_MSG(mount != nullptr, "stub mount should succeed");

    VfsListedCloseCtx ctx = { .mount = mount };
    atomic_init(&ctx.in_callback, false);
    atomic_init(&ctx.go, false);
    atomic_init(&ctx.closed_mount, false);
    atomic_init(&ctx.release, false);
    FlVfsHandle closer = vfs_mount_read_all_prepare(mount, FL_S(STUB_VFS_FILE), vfs_listed_close_callback, &ctx,
                                                    nullptr, FL_VFS_HANDLE_INVALID);
    vfs_dispatch(closer);
    const bool parked = vfs_await_flag(&ctx.in_callback);

    static VfsReleaseCtx queued;
    vfs_release_ctx_reset(&queued);
    FlVfsHandle behind = fl_vfs_mount_read_all_with_options(mount, FL_S(STUB_VFS_FILE), vfs_count_callback, &queued,
                                                            vfs_count_release, FL_VFS_HANDLE_INVALID);

    // From here nothing may ASSERT until the worker is released.
    atomic_store(&ctx.go, true);
    const bool closed = vfs_await_flag(&ctx.closed_mount);
    atomic_store(&ctx.release, true);
    vfs_wait_all();

    ASSERT_NE(behind, FL_VFS_HANDLE_INVALID);
    ASSERT_TRUE(wait_all_covered_the_closed_mount(parked, closed, &queued, &leaks));
}
