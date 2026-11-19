// VFS handle lifetime tests: several threads opening, reading, waiting on and closing handles at once

#include "vfs_test_support.h"

// Multi-threaded handle churn. Several threads run the full open/read/wait/get-result/close cycle on overlapping
// paths. Another thread takes handle ids out of shared slots and closes them, so a close races wait, get_data and
// close on the same id. The main thread closes a second mount during the churn with reads still in flight. Each
// second-mount close waits for the churn threads to complete another batch of cycles.

#define VFS_CHURN_WAIT_MS 5000
#define VFS_CHURN_BATCH 20
#define VFS_CHURN_THREADS 3
#define VFS_CHURN_SLOTS 4
#define VFS_CHURN_SECOND_MOUNTS 8
#define VFS_WAIT_ALL_CHURN_HANDLE_TARGET 2000
#define VFS_WAIT_ALL_CHURN_WAIT_TARGET 200

typedef struct VfsChurnCtx {
    FlVfsMount* mount;
    _Atomic bool stop;
    _Atomic(FlVfsHandle) slots[VFS_CHURN_SLOTS]; // churn threads publish live handles here and the closer thread takes them
    _Atomic int cycles;
} VfsChurnCtx;

// Shared handle: publish it so the closer thread races a close against the calls below. Reading the result payload
// while another thread closes the same handle is caller error, so this cycle does not call fl_vfs_get_data.
static void churn_shared_cycle(VfsChurnCtx* ctx, FlVfsHandle h, unsigned iter) {
    atomic_store(&ctx->slots[(iter / 2) % VFS_CHURN_SLOTS], h);
    fl_vfs_wait(h);
    (void)fl_vfs_is_ready(h);
    (void)vfs_get_status(h);
    fl_vfs_close(h); // the closer thread may have closed it already, and a second close is a no-op
}

// Private handle: the full open/read/wait/get-result/close cycle, including reading the payload.
static void churn_private_cycle(FlVfsHandle h) {
    fl_vfs_wait(h);
    (void)fl_vfs_is_ready(h);
    (void)vfs_get_status(h);
    FlVfsData data = fl_vfs_get_data(h);
    (void)data;
    fl_vfs_close(h);
}

static void* vfs_churn_thread(void* user_data) {
    VfsChurnCtx* ctx = user_data;
    // Every thread reads the same file, so all threads use the same driver handle
    unsigned iter = 0;
    while (!atomic_load(&ctx->stop)) {
        FlVfsHandle h = stub_read_all(ctx->mount);
        if (h == FL_VFS_HANDLE_INVALID) {
            continue;
        }

        if (iter++ & 1) {
            churn_shared_cycle(ctx, h, iter);
        } else {
            churn_private_cycle(h);
        }
        atomic_fetch_add(&ctx->cycles, 1);
    }
    return nullptr;
}

// Closes every handle published in a slot, racing the owner's wait, get_data and close on the same id.
static void churn_close_slots(VfsChurnCtx* ctx) {
    for (int i = 0; i < VFS_CHURN_SLOTS; i++) {
        FlVfsHandle h = atomic_exchange(&ctx->slots[i], FL_VFS_HANDLE_INVALID);
        if (h != FL_VFS_HANDLE_INVALID) {
            fl_vfs_close(h);
        }
    }
}

static void* vfs_closer_thread(void* user_data) {
    VfsChurnCtx* ctx = user_data;
    while (!atomic_load(&ctx->stop)) {
        churn_close_slots(ctx);
        // The closer also closes a handle of its own while the read job may still be pending
        FlVfsHandle h = stub_read_all(ctx->mount);
        if (h != FL_VFS_HANDLE_INVALID) {
            fl_vfs_close(h);
        }
    }
    return nullptr;
}

static void churn_wait_for_cycles(const VfsChurnCtx* ctx, int target) {
    for (int waited = 0; waited < VFS_CHURN_WAIT_MS && atomic_load(&ctx->cycles) < target; waited++) {
        sleep_ms(1);
    }
}

// Closes a second mount with reads still in flight, so its drain runs while the other threads use the handle
// map. The churn threads never touch this mount. False when the mount does not come up.
static bool churn_close_second_mount(const VfsChurnCtx* ctx) {
    FlVfsMountResult second = vfs_mount(FL_S("/stub"));
    if (second.status != FlVfsMountErrorStatus_Success) {
        return false;
    }
    (void)vfs_await_until(vfs_mount_is_ready_pred, second.mount, 2000000000ull);
    if (!fl_vfs_mount_is_ready(second.mount)) {
        return false;
    }
    for (int j = 0; j < 4; j++) {
        (void)stub_read_all(second.mount);
    }
    churn_wait_for_cycles(ctx, atomic_load(&ctx->cycles) + VFS_CHURN_BATCH);
    fl_vfs_mount_close(second.mount);
    return true;
}

static bool churn_close_second_mounts(const VfsChurnCtx* ctx) {
    for (int i = 0; i < VFS_CHURN_SECOND_MOUNTS; i++) {
        if (!churn_close_second_mount(ctx)) {
            return false;
        }
    }
    return true;
}

UTEST(vfs, handle_lifetime_multithreaded_churn) {
    FlVfsMount* mount = stub_mount_ready();
    ASSERT_TRUE_MSG(mount != nullptr, "stub mount should succeed");

    VfsChurnCtx ctx = { .mount = mount };
    for (int i = 0; i < VFS_CHURN_SLOTS; i++) {
        atomic_init(&ctx.slots[i], FL_VFS_HANDLE_INVALID);
    }

    Thread churn[VFS_CHURN_THREADS];
    Thread closer;
    for (int i = 0; i < VFS_CHURN_THREADS; i++) {
        ASSERT_TRUE(os_thread_create(&churn[i], vfs_churn_thread, &ctx));
    }
    ASSERT_TRUE(os_thread_create(&closer, vfs_closer_thread, &ctx));

    const bool second_mounts_closed = churn_close_second_mounts(&ctx);

    atomic_store(&ctx.stop, true);
    for (int i = 0; i < VFS_CHURN_THREADS; i++) {
        os_thread_join(&churn[i]);
    }
    os_thread_join(&closer);

    // Close any handle still in a slot when the loops stopped
    churn_close_slots(&ctx);

    ASSERT_TRUE(second_mounts_closed);
    ASSERT_GT(atomic_load(&ctx.cycles), 0);

    // A normal read still works afterwards
    ASSERT_TRUE_MSG(stub_read_succeeds(mount, "read after multithreaded churn should still succeed"),
                    "a normal read works");

    fl_vfs_mount_close(mount);
}

// vfs_wait_all collects the jobs of every live op while other threads create and close ops. Each churn thread
// closes only its own handles, which keeps this race apart from the same-handle races covered above.

typedef struct VfsWaitAllChurnCtx {
    FlVfsMount* mount;
    _Atomic bool stop;
    _Atomic int handle_cycles;
    _Atomic int wait_cycles;
} VfsWaitAllChurnCtx;

static void* vfs_wait_all_handle_churn_thread(void* user_data) {
    VfsWaitAllChurnCtx* ctx = user_data;
    while (!atomic_load(&ctx->stop)) {
        FlVfsHandle handle = stub_read_all(ctx->mount);
        if (handle != FL_VFS_HANDLE_INVALID) {
            fl_vfs_wait(handle);
            fl_vfs_close(handle);
            atomic_fetch_add(&ctx->handle_cycles, 1);
        }
    }
    return nullptr;
}

static void* vfs_wait_all_churn_thread(void* user_data) {
    VfsWaitAllChurnCtx* ctx = user_data;
    while (!atomic_load(&ctx->stop)) {
        vfs_wait_all();
        atomic_fetch_add(&ctx->wait_cycles, 1);
    }
    return nullptr;
}

static bool wait_all_churn_reached_targets(const void* arg) {
    const VfsWaitAllChurnCtx* ctx = arg;
    return atomic_load(&ctx->handle_cycles) >= VFS_WAIT_ALL_CHURN_HANDLE_TARGET
           && atomic_load(&ctx->wait_cycles) >= VFS_WAIT_ALL_CHURN_WAIT_TARGET;
}

UTEST(vfs, wait_all_concurrent_op_churn) {
    FlVfsMount* mount = stub_mount_started();
    ASSERT_TRUE_MSG(mount != nullptr, "stub mount should succeed");

    VfsWaitAllChurnCtx ctx = { .mount = mount };
    Thread churn[VFS_CHURN_THREADS];
    Thread waiter;
    for (int i = 0; i < VFS_CHURN_THREADS; ++i) {
        ASSERT_TRUE(os_thread_create(&churn[i], vfs_wait_all_handle_churn_thread, &ctx));
    }
    ASSERT_TRUE(os_thread_create(&waiter, vfs_wait_all_churn_thread, &ctx));

    (void)vfs_await_until(wait_all_churn_reached_targets, &ctx, VFS_AWAIT_SHORT_NS);
    atomic_store(&ctx.stop, true);
    for (int i = 0; i < VFS_CHURN_THREADS; ++i) {
        os_thread_join(&churn[i]);
    }
    os_thread_join(&waiter);

    ASSERT_GE(atomic_load(&ctx.handle_cycles), VFS_WAIT_ALL_CHURN_HANDLE_TARGET);
    ASSERT_GE(atomic_load(&ctx.wait_cycles), VFS_WAIT_ALL_CHURN_WAIT_TARGET);
    fl_vfs_mount_close(mount);
}

// Reads chained onto a file keep the file alive past its own close, so the file's driver handle is never closed
// under a read that is queued or running.

static bool chained_reads_deliver_the_stub_bytes(FlVfsHandle file) {
    u8 buf1[sizeof(k_stub_vfs_data)] = { 0 };
    u8 buf2[sizeof(k_stub_vfs_data)] = { 0 };
    FlVfsHandle r1 = fl_vfs_read(file, buf1, sizeof(buf1)); // chains onto the open job, which may not have finished
    FlVfsHandle r2 = fl_vfs_read(file, buf2, sizeof(buf2)); // chains onto r1
    if (r1 == FL_VFS_HANDLE_INVALID || r2 == FL_VFS_HANDLE_INVALID) {
        return false;
    }

    fl_vfs_wait(r2); // r2 depends on r1 depends on open, so this drains the whole chain
    FlVfsData d1 = fl_vfs_get_data(r1);
    const bool first_ok = vfs_check(d1.success, "first chained read should succeed")
                          && vfs_check((size_t)d1.size == sizeof(k_stub_vfs_data), "first read returns the file")
                          && vfs_check(memcmp(buf1, k_stub_vfs_data, sizeof(k_stub_vfs_data)) == 0, "first read bytes");
    const bool second_ok = vfs_check(fl_vfs_get_data(r2).success, "second chained read should succeed (EOF, 0 bytes)");

    fl_vfs_close(r1);
    fl_vfs_close(r2);
    return first_ok && second_ok;
}

// Closes the file with a chained read still pending. The loop makes it likely the worker is mid-chain.
static bool file_closed_with_a_read_pending_still_reads(FlVfsMount* mount) {
    for (int i = 0; i < 16; ++i) {
        FlVfsHandle f = fl_vfs_mount_open(mount, FL_S(STUB_VFS_FILE), FlVfsOpenFlags_Read);
        if (f == FL_VFS_HANDLE_INVALID) {
            return false;
        }
        u8 buf[sizeof(k_stub_vfs_data)] = { 0 };
        FlVfsHandle r = fl_vfs_read(f, buf, sizeof(buf));
        if (r == FL_VFS_HANDLE_INVALID) {
            return false;
        }
        fl_vfs_close(f); // returns at once, and the file handle is kept until the chained read has run
        // The read handle outlives its file's close and its result can still be read.
        fl_vfs_wait(r);
        const bool ok = fl_vfs_is_ready(r) && fl_vfs_get_data(r).success;
        fl_vfs_close(r);
        if (!ok) {
            return false;
        }
    }
    return true;
}

UTEST(vfs, file_handle_chained_reads_survive_file_close) {
    FlVfsMount* mount = stub_mount_ready();
    ASSERT_TRUE_MSG(mount != nullptr, "stub mount should succeed");

    FlVfsHandle file = fl_vfs_mount_open(mount, FL_S(STUB_VFS_FILE), FlVfsOpenFlags_Read);
    ASSERT_NE(file, FL_VFS_HANDLE_INVALID);
    ASSERT_TRUE(chained_reads_deliver_the_stub_bytes(file));
    fl_vfs_close(file);

    ASSERT_TRUE(file_closed_with_a_read_pending_still_reads(mount));

    fl_vfs_mount_close(mount);
}
