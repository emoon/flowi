// VFS close tests: closing ops and file handles that are queued, running or already finished

#include "vfs_test_support.h"

static bool release_ctx_released_once(const void* arg) {
    return atomic_load(&((const VfsReleaseCtx*)arg)->release_calls) == 1;
}

// fl_vfs_close() of a handle whose job has not run returns at once and leaves the job to finish on its own. The
// job releases the op when it ends. A close that waited would wait on a gate this thread opens only after the
// close has returned.

typedef struct QueuedCloseFindings {
    FlVfsHandle victim;
    int calls_at_close;
    int releases_at_close;
    bool absent_after_close;
    bool released_by_job;
} QueuedCloseFindings;

// Closes a read queued behind the parked worker, then lets the worker go and waits for the job to release the op.
// Nothing here asserts, so the parker is never left parked.
static QueuedCloseFindings close_queued_read(FlVfsMount* mount, VfsParkCtx* park, FlVfsHandle parker,
                                             VfsReleaseCtx* ctx) {
    QueuedCloseFindings found
        = { .victim = fl_vfs_mount_read_all_with_options(mount, FL_S(STUB_VFS_FILE), vfs_count_callback, ctx,
                                                         vfs_count_release, FL_VFS_HANDLE_INVALID) };

    // The worker is parked, so the read cannot have run. A close that waited for it would never return, since
    // the release below runs on this thread after it.
    fl_vfs_close(found.victim);
    found.calls_at_close = atomic_load(&ctx->callback_calls);
    found.releases_at_close = atomic_load(&ctx->release_calls);
    found.absent_after_close = vfs_get_status(found.victim) == VFS_STATE_ERROR;

    atomic_store(&park->release, true);
    fl_vfs_wait(parker);

    // The victim's job runs once the worker is free, and releases the op as it ends. Nothing here closes or waits
    // on anything else.
    found.released_by_job = vfs_await_until(release_ctx_released_once, ctx, VFS_AWAIT_SHORT_NS);
    return found;
}

static bool queued_read_close_was_prompt(bool parked, const QueuedCloseFindings* found, const VfsReleaseCtx* ctx) {
    return vfs_check(parked, "the parking callback never ran")
           && vfs_check(found->victim != FL_VFS_HANDLE_INVALID, "the queued read has an id")
           && vfs_check(found->calls_at_close == 0, "the close must return before the queued read has run")
           && vfs_check(found->releases_at_close == 0, "user_data must not be released while the job can still reach it")
           && vfs_check(found->released_by_job, "the job must release the closed op when it ends")
           && vfs_check(found->absent_after_close, "a closed handle must read as absent at once")
           && vfs_release_ctx_is(ctx, 1, 1);
}

// A finished read releases at its close, on the closer's thread.
static bool finished_read_releases_at_close(FlVfsMount* mount) {
    static VfsReleaseCtx finished;
    vfs_release_ctx_reset(&finished);
    FlVfsHandle done = fl_vfs_mount_read_all_with_options(mount, FL_S(STUB_VFS_FILE), vfs_count_callback, &finished,
                                                          vfs_count_release, FL_VFS_HANDLE_INVALID);
    if (done == FL_VFS_HANDLE_INVALID) {
        return false;
    }
    fl_vfs_wait(done);
    const bool kept_until_close = vfs_check(atomic_load(&finished.release_calls) == 0, "released before its close");
    fl_vfs_close(done);
    return kept_until_close
           && vfs_check(atomic_load(&finished.release_calls) == 1, "an immediate close releases user_data")
           && vfs_check(!atomic_load(&finished.released_before_callback), "the release must follow the callback");
}

// A handle that is never closed is released by its mount's close, release hook included. Every leaked read
// takes this path at shutdown.
static bool leaked_read_is_released_by_mount_close(FlVfsMount* mount, FlVfsHandle parker) {
    static VfsReleaseCtx leaked;
    vfs_release_ctx_reset(&leaked);
    FlVfsHandle never_closed = fl_vfs_mount_read_all_with_options(mount, FL_S(STUB_VFS_FILE), vfs_count_callback,
                                                                  &leaked, vfs_count_release, FL_VFS_HANDLE_INVALID);
    if (never_closed == FL_VFS_HANDLE_INVALID) {
        return false;
    }

    fl_vfs_close(parker);
    fl_vfs_mount_close(mount);
    return vfs_release_ctx_is(&leaked, 1, 1);
}

UTEST(vfs, close_of_a_queued_read_returns_and_the_job_releases_it) {
    FlVfsMount* mount = stub_mount_ready();
    ASSERT_TRUE_MSG(mount != nullptr, "stub mount should succeed");

    VfsParkCtx park;
    FlVfsHandle parker;
    const bool parked = vfs_park_worker(&park, mount, &parker, FL_S(STUB_VFS_FILE));

    static VfsReleaseCtx ctx;
    vfs_release_ctx_reset(&ctx);
    const QueuedCloseFindings found = close_queued_read(mount, &park, parker, &ctx);

    ASSERT_TRUE(queued_read_close_was_prompt(parked, &found, &ctx));
    ASSERT_TRUE(finished_read_releases_at_close(mount));
    ASSERT_TRUE(leaked_read_is_released_by_mount_close(mount, parker));
}

// Closing a file handle with a read chained onto it also returns at once. The file stays alive until the read
// has run, the read still gets its bytes, and nothing chains onto the closed file afterwards.

typedef struct QueuedFileCtx {
    FlVfsHandle file;
    FlVfsHandle read;
    FlVfsHandle late_read;
    bool absent_after_close;
    u8 buffer[sizeof(k_stub_vfs_data)];
    u8 late_buffer[sizeof(k_stub_vfs_data)];
} QueuedFileCtx;

// Closes a file whose open is queued behind the parked worker, with a read chained onto it, then lets the worker go.
static void close_queued_file(QueuedFileCtx* queued, FlVfsMount* mount, VfsParkCtx* park, FlVfsHandle parker) {
    queued->file = fl_vfs_mount_open(mount, FL_S(STUB_VFS_FILE), FlVfsOpenFlags_Read); // queued behind parker
    queued->read = fl_vfs_read(queued->file, queued->buffer, sizeof(queued->buffer)); // chained onto the queued open

    fl_vfs_close(queued->file); // returns although the open has not run
    queued->absent_after_close = !fl_vfs_is_ready(queued->file);
    queued->late_read = fl_vfs_read(queued->file, queued->late_buffer, sizeof(queued->late_buffer));

    atomic_store(&park->release, true);
    fl_vfs_wait(parker);
    fl_vfs_wait(queued->read);
}

static bool queued_file_closed_at_once(bool parked, const QueuedFileCtx* queued) {
    return vfs_check(parked, "the parking callback never ran")
           && vfs_check(queued->file != FL_VFS_HANDLE_INVALID && queued->read != FL_VFS_HANDLE_INVALID, "handles")
           && vfs_check(queued->absent_after_close, "a closed file handle must read as absent at once")
           && vfs_check(queued->late_read == FL_VFS_HANDLE_INVALID, "nothing chains onto a closed file handle");
}

static bool chained_read_outlived_the_file_close(const QueuedFileCtx* queued) {
    FlVfsData data = fl_vfs_get_data(queued->read);
    return vfs_check(data.success, "the open ran and its file stayed alive for the read chained before the close")
           && vfs_check((size_t)data.size == sizeof(k_stub_vfs_data), "the read returns the file")
           && vfs_check(memcmp(queued->buffer, k_stub_vfs_data, sizeof(k_stub_vfs_data)) == 0, "the bytes");
}

UTEST(vfs, close_of_a_queued_file_handle_returns_and_its_chained_read_still_completes) {
    FlVfsMount* mount = stub_mount_ready();
    ASSERT_TRUE_MSG(mount != nullptr, "stub mount should succeed");

    VfsParkCtx park;
    FlVfsHandle parker;
    const bool parked = vfs_park_worker(&park, mount, &parker, FL_S(STUB_VFS_FILE));

    QueuedFileCtx queued = { 0 };
    close_queued_file(&queued, mount, &park, parker);

    ASSERT_TRUE(queued_file_closed_at_once(parked, &queued));
    ASSERT_TRUE(chained_read_outlived_the_file_close(&queued));
    fl_vfs_close(queued.read);

    vfs_wait_all(); // the chain has run, so the file is gone
    FlVfsHandle reused
        = vfs_mount_read_all_prepare(mount, FL_S(STUB_VFS_FILE), nullptr, nullptr, nullptr, queued.file);
    ASSERT_EQ_MSG(reused, queued.file, "a closed file id must be reusable");
    fl_vfs_close(reused);

    fl_vfs_close(parker);
    fl_vfs_mount_close(mount);
}

// Closing an id retires it, and the id stays reserved until the old op has finished and been destroyed, so a
// reload under the same id cannot race the closed op's completion. Reuse of an id that is still open or retiring
// is refused with a warning, and succeeds once the old op is gone.

typedef struct ReservedIdFindings {
    bool parked;
    FlVfsHandle id;
    FlVfsHandle parker;
    bool absent_after_close;
    FlVfsHandle refused_open;
    FlVfsHandle refused_retiring;
    FlVfsHandle reused;
} ReservedIdFindings;

// Closes a read queued behind the parked worker, and tries to take its id while it is open and while it is retiring.
static ReservedIdFindings close_a_queued_id(FlVfsMount* mount, VfsReleaseCtx* first) {
    ReservedIdFindings found = { 0 };
    found.id = fl_vfs_mount_read_all_with_options(mount, FL_S(STUB_VFS_FILE), vfs_count_callback, first,
                                                  vfs_count_release, FL_VFS_HANDLE_INVALID); // queued behind parker
    found.refused_open
        = vfs_mount_read_all_prepare(mount, FL_S(STUB_VFS_FILE), nullptr, nullptr, nullptr, found.id);
    fl_vfs_close(found.id);
    found.absent_after_close = !fl_vfs_is_ready(found.id);
    found.refused_retiring
        = vfs_mount_read_all_prepare(mount, FL_S(STUB_VFS_FILE), nullptr, nullptr, nullptr, found.id);
    return found;
}

// Lets the parked worker go, waits for the closed op's job too, and takes the id again.
static void reuse_the_closed_id(ReservedIdFindings* found, FlVfsMount* mount, VfsReleaseCtx* second) {
    fl_vfs_wait(found->parker);
    vfs_wait_all(); // the closed op's job is waited for too, and releases the op as it ends
    found->reused = fl_vfs_mount_read_all_with_options(mount, FL_S(STUB_VFS_FILE), vfs_count_callback, second,
                                                       vfs_count_release, found->id);
    fl_vfs_wait(found->reused);
}

static bool closed_id_stayed_reserved_until_its_op_ran(const ReservedIdFindings* found) {
    return vfs_check(found->parked, "the parking callback never ran")
           && vfs_check(found->id != FL_VFS_HANDLE_INVALID, "the read has an id")
           && vfs_check(found->absent_after_close, "a closed id must read as absent at once")
           && vfs_check(found->refused_open == FL_VFS_HANDLE_INVALID, "an id that is still open cannot be reused")
           && vfs_check(found->refused_retiring == FL_VFS_HANDLE_INVALID,
                        "a closed id whose op is still queued cannot be reused")
           && vfs_check(found->reused == found->id, "the id is free again once its old op is gone");
}

static bool closed_op_released_and_reusing_op_still_open(const VfsReleaseCtx* first, const VfsReleaseCtx* second) {
    return vfs_release_ctx_counts(first, 1, 1) && vfs_release_ctx_counts(second, 1, 0);
}

UTEST(vfs, a_closed_id_stays_reserved_until_its_old_op_has_run) {
    FlVfsMount* mount = stub_mount_ready();
    ASSERT_TRUE_MSG(mount != nullptr, "stub mount should succeed");

    VfsParkCtx park;
    FlVfsHandle parker;
    const bool parked = vfs_park_worker(&park, mount, &parker, FL_S(STUB_VFS_FILE));

    static VfsReleaseCtx first;
    static VfsReleaseCtx second;
    vfs_release_ctx_reset(&first);
    vfs_release_ctx_reset(&second);
    ReservedIdFindings found = close_a_queued_id(mount, &first);
    found.parked = parked;
    found.parker = parker;
    atomic_store(&park.release, true);
    reuse_the_closed_id(&found, mount, &second);

    ASSERT_TRUE(closed_id_stayed_reserved_until_its_op_ran(&found));
    ASSERT_TRUE(closed_op_released_and_reusing_op_still_open(&first, &second));
    ASSERT_TRUE_MSG(fl_vfs_get_data(found.reused).success, "the id names the new op, not the closed one");

    fl_vfs_close(found.reused);
    ASSERT_EQ(atomic_load(&second.release_calls), 1);
    ASSERT_EQ(atomic_load(&first.release_calls), 1);

    fl_vfs_close(parker);
    fl_vfs_mount_close(mount);
}

// A read hands its file back as soon as it has run, before its own id is closed. Closing the file id afterwards
// closes the driver's file handle at once, while the read's id is still open.

UTEST(vfs, a_read_that_has_run_no_longer_keeps_its_file_open) {
    FlVfsMount* mount = stub_mount_ready();
    ASSERT_TRUE_MSG(mount != nullptr, "stub mount should succeed");

    FlVfsHandle file = fl_vfs_mount_open(mount, FL_S(STUB_VFS_FILE), FlVfsOpenFlags_Read);
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
    fl_vfs_mount_close(mount);
}

// vfs_dispatch schedules an op once. Dispatching a prepared op twice runs it once, and dispatching a read that a
// worker already ran inline (its file reference handed back) does not run it again.

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

static bool op_dispatched_twice_ran_once(FlVfsMount* mount) {
    static VfsReleaseCtx counts;
    vfs_release_ctx_reset(&counts);
    FlVfsHandle twice = vfs_mount_read_all_prepare(mount, FL_S(STUB_VFS_FILE), vfs_count_callback, &counts,
                                                   vfs_count_release, FL_VFS_HANDLE_INVALID);
    vfs_dispatch(twice);
    vfs_dispatch(twice);
    vfs_wait_all();
    fl_vfs_close(twice);
    return vfs_check(atomic_load(&counts.callback_calls) == 1, "a second dispatch must not run the op again")
           && vfs_check(atomic_load(&counts.release_calls) == 1, "user_data is released once");
}

// The read ran inline on the worker, so dispatching it again must not read the file again.
static bool inline_read_is_not_dispatched_again(FlVfsMount* mount) {
    FlVfsHandle file = fl_vfs_mount_open(mount, FL_S(STUB_VFS_FILE), FlVfsOpenFlags_Read);
    fl_vfs_wait(file);
    static VfsInlineReadCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    atomic_init(&ctx.file, file);
    atomic_init(&ctx.read, FL_VFS_HANDLE_INVALID);
    atomic_init(&ctx.done, false);
    FlVfsHandle reader = vfs_mount_read_all_prepare(mount, FL_S(STUB_VFS_FILE), vfs_inline_read_callback, &ctx,
                                                    nullptr, FL_VFS_HANDLE_INVALID);
    vfs_dispatch(reader);
    const bool ran = vfs_await_flag(&ctx.done);
    FlVfsHandle read = atomic_load(&ctx.read);
    vfs_wait_all();
    const int reads_before_dispatch = s_stub_vfs_read_count;
    if (ran && read != FL_VFS_HANDLE_INVALID) {
        vfs_dispatch(read); // already ran inline on the worker
    }
    vfs_wait_all();

    const bool ok = vfs_check(ran, "the reader never ran") && vfs_check(read != FL_VFS_HANDLE_INVALID, "a read id")
                    && vfs_check(fl_vfs_get_data(read).success, "the inline read succeeds")
                    && vfs_check(memcmp(ctx.buffer, k_stub_vfs_data, sizeof(k_stub_vfs_data)) == 0, "the bytes")
                    && vfs_check(s_stub_vfs_read_count == reads_before_dispatch,
                                 "dispatching a read a worker already ran must not read again");

    fl_vfs_close(read);
    fl_vfs_close(reader);
    fl_vfs_close(file);
    return ok;
}

UTEST(vfs, dispatching_an_op_twice_runs_it_once) {
    FlVfsMount* mount = stub_mount_started();
    ASSERT_TRUE_MSG(mount != nullptr, "stub mount should succeed");

    ASSERT_TRUE(op_dispatched_twice_ran_once(mount));
    ASSERT_TRUE(inline_read_is_not_dispatched_again(mount));

    fl_vfs_mount_close(mount);
}

// fl_vfs_read touches its file after it has scheduled the read. In that gap the read can run, hand the file
// back, and have the file's id closed, and the file must still be there. A test seam inside fl_vfs_read runs the
// read to completion and closes the file id at that point. Under ASAN a missing reference on the file is a
// use-after-free.

static FlVfsHandle s_race_file;
static int s_race_closes_before;
static int s_race_closes_in_gap;

static void vfs_close_file_in_the_gap(FlVfsHandle read) {
    fl_vfs_wait(read); // on the main thread the read runs to completion and hands the file back
    fl_vfs_close(s_race_file);
    s_race_closes_in_gap = s_stub_vfs_close_count - s_race_closes_before;
}

static bool file_close_waited_for_the_scheduling_gap(void) {
    return vfs_check(s_race_closes_in_gap == 0, "fl_vfs_read still uses the file, so closing its id must not free it yet")
           && vfs_check(s_stub_vfs_close_count - s_race_closes_before == 1,
                        "the file goes once fl_vfs_read is done with it");
}

static bool read_survived_the_gap_close(FlVfsHandle read, const u8* buffer) {
    return vfs_check(fl_vfs_get_data(read).success, "the read succeeded")
           && vfs_check(memcmp(buffer, k_stub_vfs_data, sizeof(k_stub_vfs_data)) == 0, "the bytes");
}

UTEST(vfs, closing_a_file_while_its_read_is_being_scheduled_leaves_the_read_intact) {
    FlVfsMount* mount = stub_mount_started();
    ASSERT_TRUE_MSG(mount != nullptr, "stub mount should succeed");

    s_race_file = fl_vfs_mount_open(mount, FL_S(STUB_VFS_FILE), FlVfsOpenFlags_Read);
    fl_vfs_wait(s_race_file);
    u8 buffer[sizeof(k_stub_vfs_data)] = { 0 };

    s_race_closes_before = s_stub_vfs_close_count;
    s_race_closes_in_gap = -1;
    g_vfs_test_after_read_scheduled = vfs_close_file_in_the_gap;
    FlVfsHandle read = fl_vfs_read(s_race_file, buffer, sizeof(buffer));
    g_vfs_test_after_read_scheduled = nullptr;

    ASSERT_NE(read, FL_VFS_HANDLE_INVALID);
    ASSERT_TRUE(file_close_waited_for_the_scheduling_gap());
    ASSERT_TRUE(read_survived_the_gap_close(read, buffer));

    fl_vfs_close(read);
    fl_vfs_mount_close(mount);
}

// The release hook is caller code and runs outside every VFS lock on each path that can run it: the close of a
// finished op, and the end of the job of an op closed while it was queued. A hook that calls back into the VFS
// would deadlock on the registry lock if it ran under it.

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

static FlVfsHandle prepare_unused_read(FlVfsMount* mount) {
    return vfs_mount_read_all_prepare(mount, FL_S(STUB_VFS_FILE), nullptr, nullptr, nullptr, FL_VFS_HANDLE_INVALID);
}

UTEST(vfs, a_release_hook_may_call_back_into_the_vfs) {
    FlVfsMount* mount = stub_mount_started();
    ASSERT_TRUE_MSG(mount != nullptr, "stub mount should succeed");

    // On the closer's thread: a finished op released by its own close.
    static VfsReentrantCtx on_close;
    memset(&on_close, 0, sizeof(on_close));
    FlVfsHandle other_close = prepare_unused_read(mount);
    atomic_init(&on_close.other, other_close);
    FlVfsHandle finished = fl_vfs_mount_read_all_with_options(mount, FL_S(STUB_VFS_FILE), nullptr, &on_close,
                                                              vfs_reentrant_release, FL_VFS_HANDLE_INVALID);
    fl_vfs_wait(finished);
    fl_vfs_close(finished);
    ASSERT_EQ(atomic_load(&on_close.calls), 1);
    ASSERT_EQ_MSG(vfs_get_status(other_close), (VfsOpStatus)VFS_STATE_ERROR,
                  "the hook's own close must have taken effect");

    // On the worker: an op closed while queued, released as its job ends.
    VfsParkCtx park;
    FlVfsHandle parker;
    bool parked = vfs_park_worker(&park, mount, &parker, FL_S(STUB_VFS_FILE));
    static VfsReentrantCtx on_worker;
    memset(&on_worker, 0, sizeof(on_worker));
    FlVfsHandle other_worker = prepare_unused_read(mount);
    atomic_init(&on_worker.other, other_worker);
    FlVfsHandle queued = fl_vfs_mount_read_all_with_options(mount, FL_S(STUB_VFS_FILE), nullptr, &on_worker,
                                                            vfs_reentrant_release, FL_VFS_HANDLE_INVALID);
    fl_vfs_close(queued);
    atomic_store(&park.release, true);
    fl_vfs_wait(parker);
    vfs_wait_all();

    ASSERT_TRUE_MSG(parked, "the parking callback never ran");
    ASSERT_EQ(atomic_load(&on_worker.calls), 1);
    ASSERT_EQ(vfs_get_status(other_worker), (VfsOpStatus)VFS_STATE_ERROR);

    fl_vfs_close(parker);
    fl_vfs_mount_close(mount);
}

// A fl_vfs_read runs against the caller's buffer, and its close tells the caller the buffer may go, so this
// close does wait for the job. A helper thread opens the gate after the close has begun, and the close returns
// only after the read has filled the buffer.

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

static bool lent_buffer_close_waited(bool parked, bool thread_started, bool gate_was_open, bool buffer_filled) {
    return vfs_check(parked, "the parking callback never ran")
           && vfs_check(thread_started, "the releasing thread never started")
           && vfs_check(gate_was_open, "closing a fl_vfs_read must wait for the job the buffer is lent to")
           && vfs_check(buffer_filled, "the read must have filled the caller's buffer before its close returned");
}

UTEST(vfs, close_of_a_queued_vfs_read_waits_for_the_buffer_it_lent) {
    FlVfsMount* mount = stub_mount_ready();
    ASSERT_TRUE_MSG(mount != nullptr, "stub mount should succeed");

    VfsParkCtx park;
    FlVfsHandle parker;
    const bool parked = vfs_park_worker(&park, mount, &parker, FL_S(STUB_VFS_FILE));

    FlVfsHandle file = fl_vfs_mount_open(mount, FL_S(STUB_VFS_FILE), FlVfsOpenFlags_Read); // queued behind parker
    u8 buf[sizeof(k_stub_vfs_data)] = { 0 };
    FlVfsHandle read = fl_vfs_read(file, buf, sizeof(buf));

    VfsDelayedReleaseCtx delayed = { .park = &park };
    atomic_init(&delayed.released, false);
    Thread releaser;
    const bool thread_started = os_thread_create(&releaser, vfs_delayed_release_thread, &delayed);
    if (!thread_started) {
        atomic_store(&park.release, true);
    }

    fl_vfs_close(read); // the gate opens on the helper thread while this waits
    const bool gate_was_open = atomic_load(&delayed.released);
    const bool buffer_filled = memcmp(buf, k_stub_vfs_data, sizeof(k_stub_vfs_data)) == 0;

    if (thread_started) {
        os_thread_join(&releaser);
    }
    fl_vfs_wait(parker);

    ASSERT_NE(file, FL_VFS_HANDLE_INVALID);
    ASSERT_NE(read, FL_VFS_HANDLE_INVALID);
    ASSERT_TRUE(lent_buffer_close_waited(parked, thread_started, gate_was_open, buffer_filled));

    fl_vfs_close(file);
    fl_vfs_close(parker);
    fl_vfs_mount_close(mount);
}
