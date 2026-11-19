// image load, prepare and teardown tests

#include "render/common.h"

// The stub backend records how prepare_image was called.

typedef struct PrepareStats {
    _Atomic int call_count;
    u64 thread;         // thread that made the last call
    bool decoded_valid; // decoded != null in the last call
    bool during_pump;   // last call happened inside image_system_begin_frame
} PrepareStats;

// Set around image_system_begin_frame so the stub can tell a pump call from a job call.
static _Atomic bool s_in_pump = false;

static bool stub_prepare_image(void* user_data, FlImageLoadRequestId request_id, const FlImageDecodeResult* decoded,
                               FlArena* scratch, const FlImageLoadRequest* request) {
    (void)request_id;
    (void)scratch;
    (void)request;

    PrepareStats* stats = (PrepareStats*)user_data;
    stats->thread = os_thread_get_current_id();
    stats->decoded_valid = (decoded != nullptr);
    stats->during_pump = atomic_load(&s_in_pump);
    atomic_fetch_add(&stats->call_count, 1);
    return false;
}

// A tiny valid image so image_decode succeeds. SVG lets it be embedded as text.
static const char s_svg[] = "<svg width=\"8\" height=\"8\"><rect width=\"8\" height=\"8\" fill=\"#ff0000\"/></svg>";

#define PREPARE_POLL_TIMEOUT_MS 5000

// With prepare_image_on_job unset the call is deferred to the begin-frame pump on this (main, render) thread.

UTEST(prepare_image, deferred_to_begin_frame_pump) {
    PrepareStats stats = { 0 };
    FlRenderBackend backend = {
        .user_data = &stats,
        .prepare_image = stub_prepare_image,
        .prepare_image_on_job = false,
    };
    ImageSystem* sys = image_system_create(s_arena, &backend);
    ASSERT_TRUE(sys != nullptr);

    FlImageLoadRequestId id = image_system_load_from_memory(sys, s_svg, sizeof(s_svg) - 1, nullptr, nullptr);
    ASSERT_NE(id, (FlImageLoadRequestId)FL_IMAGE_LOAD_REQUEST_ID_INVALID);

    // Decode runs in the job worker. Once it completes the request is queued and not called.
    int waited = 0;
    while (!image_system_prepare_pending(sys, id) && waited < PREPARE_POLL_TIMEOUT_MS) {
        sleep_ms(1);
        waited++;
    }
    ASSERT_TRUE(image_system_prepare_pending(sys, id));
    ASSERT_EQ(atomic_load(&stats.call_count), 0); // not called from the job

    // The pump makes the call exactly once, on this thread.
    atomic_store(&s_in_pump, true);
    image_system_begin_frame(sys);
    atomic_store(&s_in_pump, false);

    const BoolCase pumped[] = {
        { atomic_load(&stats.call_count) == 1, true, "the pump makes the call exactly once" },
        { stats.thread == os_thread_get_current_id(), true, "on this thread" },
        { stats.during_pump, true, "inside the pump" },
        { stats.decoded_valid, true, "with the decoded image" },
        { image_system_prepare_pending(sys, id), false, "and the request is no longer pending" },
    };
    check_bool_cases(utest_result, pumped, sizeof_array(pumped));

    // Further pumps do not call again for the same decode.
    image_system_begin_frame(sys);
    image_system_begin_frame(sys);
    ASSERT_EQ(atomic_load(&stats.call_count), 1);

    image_system_destroy(sys);
}

// With prepare_image_on_job set the call happens in the job worker thread, as the SW backend needs.

UTEST(prepare_image, job_context_when_flag_set) {
    PrepareStats stats = { 0 };
    FlRenderBackend backend = {
        .user_data = &stats,
        .prepare_image = stub_prepare_image,
        .prepare_image_on_job = true,
    };
    ImageSystem* sys = image_system_create(s_arena, &backend);
    ASSERT_TRUE(sys != nullptr);

    FlImageLoadRequestId id = image_system_load_from_memory(sys, s_svg, sizeof(s_svg) - 1, nullptr, nullptr);
    ASSERT_NE(id, (FlImageLoadRequestId)FL_IMAGE_LOAD_REQUEST_ID_INVALID);

    // No pumping: the decode job itself calls prepare_image.
    int waited = 0;
    while (atomic_load(&stats.call_count) == 0 && waited < PREPARE_POLL_TIMEOUT_MS) {
        sleep_ms(1);
        waited++;
    }
    const BoolCase called[] = {
        { atomic_load(&stats.call_count) == 1, true, "the decode job calls prepare_image once" },
        { stats.thread != os_thread_get_current_id(), true, "on a worker thread" },
        { stats.during_pump, false, "outside the pump" },
        { stats.decoded_valid, true, "with the decoded image" },
        { image_system_prepare_pending(sys, id), false, "and nothing is left pending" },
    };
    check_bool_cases(utest_result, called, sizeof_array(called));

    image_system_destroy(sys);
}

// A memory load whose decode succeeds but that no backend acknowledges ends as failed and does not stay pending.
// Either there is no backend, or the backend's prepare_image never puts the image in the registry.

static bool wait_synthetic_settled(ImageSystem* sys, FlImageLoadRequestId id) {
    for (int waited = 0; waited < PREPARE_POLL_TIMEOUT_MS; waited++) {
        if (!image_system_load_is_active_synthetic(sys, id)) {
            return true;
        }
        sleep_ms(1);
    }
    return false;
}

UTEST(prepare_image, memory_load_without_backend_fails) {
    ImageSystem* sys = image_system_create(s_arena, nullptr);
    ASSERT_TRUE(sys != nullptr);

    FlImageLoadRequestId id = image_system_load_from_memory(sys, s_svg, sizeof(s_svg) - 1, nullptr, nullptr);
    ASSERT_NE(id, (FlImageLoadRequestId)FL_IMAGE_LOAD_REQUEST_ID_INVALID);

    ASSERT_TRUE_MSG(wait_synthetic_settled(sys, id), "a load nothing can acknowledge must leave the active set");
    ASSERT_TRUE_MSG(image_system_load_failed(sys, id), "an unacknowledged memory load must be marked failed");

    image_system_destroy(sys);
}

UTEST(prepare_image, memory_load_unacknowledged_by_job_backend_fails) {
    PrepareStats stats = { 0 };
    FlRenderBackend backend = {
        .user_data = &stats,
        .prepare_image = stub_prepare_image,
        .prepare_image_on_job = true,
    };
    ImageSystem* sys = image_system_create(s_arena, &backend);
    ASSERT_TRUE(sys != nullptr);

    FlImageLoadRequestId id = image_system_load_from_memory(sys, s_svg, sizeof(s_svg) - 1, nullptr, nullptr);
    ASSERT_NE(id, (FlImageLoadRequestId)FL_IMAGE_LOAD_REQUEST_ID_INVALID);

    ASSERT_TRUE_MSG(wait_synthetic_settled(sys, id), "the load must leave the active set once the backend returned");
    ASSERT_EQ(atomic_load(&stats.call_count), 1);
    ASSERT_TRUE_MSG(image_system_load_failed(sys, id), "a backend that never acks must leave the load failed");

    image_system_destroy(sys);
}

UTEST(prepare_image, memory_load_stays_active_until_deferred_pump) {
    PrepareStats stats = { 0 };
    FlRenderBackend backend = {
        .user_data = &stats,
        .prepare_image = stub_prepare_image,
        .prepare_image_on_job = false,
    };
    ImageSystem* sys = image_system_create(s_arena, &backend);
    ASSERT_TRUE(sys != nullptr);

    FlImageLoadRequestId id = image_system_load_from_memory(sys, s_svg, sizeof(s_svg) - 1, nullptr, nullptr);
    ASSERT_NE(id, (FlImageLoadRequestId)FL_IMAGE_LOAD_REQUEST_ID_INVALID);

    for (int waited = 0; !image_system_prepare_pending(sys, id) && waited < PREPARE_POLL_TIMEOUT_MS; waited++) {
        sleep_ms(1);
    }
    ASSERT_TRUE(image_system_prepare_pending(sys, id));
    ASSERT_TRUE_MSG(image_system_load_is_active_synthetic(sys, id), "a queued prepare is still awaiting its ack");
    ASSERT_FALSE(image_system_load_failed(sys, id));

    image_system_begin_frame(sys);

    ASSERT_FALSE_MSG(image_system_load_is_active_synthetic(sys, id), "the pump settles the load");
    ASSERT_TRUE_MSG(image_system_load_failed(sys, id), "a pumped prepare that never acks leaves the load failed");

    image_system_destroy(sys);
}

// image_system_load copies the caller's path FlString before returning. The VFS callback reads the path on a
// worker thread after the call returns, and ui.h lets the caller free or reuse its buffer at once ("path need
// only live for this call"). The path is built in a mutable stack buffer, so is_static is 0 and the copy is
// made.

typedef struct PathCaptureStats {
    _Atomic int call_count;
    char path[256];
    int path_len;
} PathCaptureStats;

static bool capture_path_prepare_image(void* user_data, FlImageLoadRequestId request_id,
                                       const FlImageDecodeResult* decoded, FlArena* scratch,
                                       const FlImageLoadRequest* request) {
    (void)request_id;
    (void)decoded;
    (void)scratch;
    PathCaptureStats* stats = (PathCaptureStats*)user_data;
    int n = (int)request->relative_path.length;
    if (n > (int)sizeof(stats->path) - 1) {
        n = (int)sizeof(stats->path) - 1;
    }
    memcpy(stats->path, request->relative_path.data, (size_t)n);
    stats->path[n] = '\0';
    stats->path_len = n;
    atomic_fetch_add(&stats->call_count, 1);
    return false;
}

UTEST(image_load, copies_caller_path_before_async_read) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    vfs_wait_all(); // drain the async mount before reading
    ASSERT_TRUE(fl_vfs_mount_is_ready(mount.mount));

    PathCaptureStats stats = { 0 };
    FlRenderBackend backend = {
        .user_data = &stats,
        .prepare_image = capture_path_prepare_image,
        .prepare_image_on_job = true, // the call is made on the worker thread, so no begin-frame pump is needed
    };
    ImageSystem* sys = image_system_create(s_arena, &backend);
    ASSERT_TRUE(sys != nullptr);

    char path_buf[] = STUB_VFS_FILE;
    FlString path = string_from_cstr_len(path_buf, sizeof(path_buf) - 1);

    const ImageLoadSource source = { .mount = mount.mount, .relative_path = path };
    FlImageLoadRequestId id = image_system_load(sys, &source, FL_IMAGE_LOAD_REQUEST_ID_INVALID);
    ASSERT_NE(id, (FlImageLoadRequestId)FL_IMAGE_LOAD_REQUEST_ID_INVALID);

    // Overwrite the caller's buffer at once, as the contract permits.
    memset(path_buf, 'X', sizeof(path_buf) - 1);

    int waited = 0;
    while (atomic_load(&stats.call_count) == 0 && waited < PREPARE_POLL_TIMEOUT_MS) {
        sleep_ms(1);
        waited++;
    }
    ASSERT_EQ(atomic_load(&stats.call_count), 1);
    ASSERT_EQ(stats.path_len, (int)sizeof(STUB_VFS_FILE) - 1);
    ASSERT_EQ_MSG(memcmp(stats.path, STUB_VFS_FILE, sizeof(STUB_VFS_FILE) - 1), 0,
                  "worker must read the owned path copy, not the caller's overwritten buffer");

    image_system_destroy(sys);
    fl_vfs_mount_close(mount.mount);
}

// Every image load takes a JobContext pool slot, and the job system does not recycle it: the slot comes back only
// if image_system releases it. The pool's free list is LIFO, so a slot put on it before a load comes back from
// the next allocation exactly when the load returned its own.
//
// The pool is process-global and shared with the glyph-atlas jobs, so unrelated traffic can disturb a single
// round. The case counts rounds: a leak misses every round, and interference misses a few.

#define CONTEXT_RECYCLE_ROUNDS 8

static int count_recycled_context_slots(ImageSystem* sys, FlVfsMount* mount, FlString path) {
    int recycled = 0;

    for (int i = 0; i < CONTEXT_RECYCLE_ROUNDS; i++) {
        void* parked = fl_jobs_alloc_context(sizeof(void*));
        if (!parked) {
            return -1;
        }
        jobs_context_free(parked);

        const ImageLoadSource source = { .mount = mount, .relative_path = path };
        FlImageLoadRequestId id = image_system_load(sys, &source, FL_IMAGE_LOAD_REQUEST_ID_INVALID);
        if (id == (FlImageLoadRequestId)FL_IMAGE_LOAD_REQUEST_ID_INVALID) {
            return -1;
        }
        vfs_wait_all();

        void* reclaimed = fl_jobs_alloc_context(sizeof(void*));
        if (reclaimed == parked) {
            recycled++;
        }
        jobs_context_free(reclaimed);
        fl_vfs_close(id);
    }

    return recycled;
}

UTEST(image_load, load_recycles_job_context_slot) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    vfs_wait_all();

    FlRenderBackend backend = { 0 };
    ImageSystem* sys = image_system_create(s_arena, &backend);
    ASSERT_TRUE(sys != nullptr);

    int completed = count_recycled_context_slots(sys, mount.mount, FL_S(STUB_VFS_FILE));
    ASSERT_TRUE_MSG(completed >= CONTEXT_RECYCLE_ROUNDS - 2, "a completed load must return its context slot");

    // A missing file fails inside the read job and not at prepare time, so the slot comes back only because the
    // VFS calls the callback on failure.
    int missing = count_recycled_context_slots(sys, mount.mount, FL_S("no_such_file.png"));
    ASSERT_TRUE_MSG(missing >= CONTEXT_RECYCLE_ROUNDS - 2, "a failed read must return its context slot");

    image_system_destroy(sys);
    fl_vfs_mount_close(mount.mount);
}

// image_system_destroy does not return while a worker can still reach the backend, and jobs that have not reached
// the backend yet skip it once teardown has begun.

typedef struct GatedBackend {
    _Atomic bool gate_open; // workers block in prepare_image until this is set
    _Atomic int entered;    // prepare_image calls that started
    _Atomic int finished;   // prepare_image calls that returned
} GatedBackend;

static bool gated_prepare_image(void* user_data, FlImageLoadRequestId request_id, const FlImageDecodeResult* decoded,
                                FlArena* scratch, const FlImageLoadRequest* request) {
    (void)request_id;
    (void)decoded;
    (void)scratch;
    (void)request;

    GatedBackend* b = (GatedBackend*)user_data;
    atomic_fetch_add(&b->entered, 1);
    while (!atomic_load(&b->gate_open)) {
        sleep_ms(1);
    }
    atomic_fetch_add(&b->finished, 1);
    return false;
}

// Runs image_system_destroy (or only image_system_close) off the test thread so the test can see it block.
typedef struct DestroyThread {
    ImageSystem* sys;
    bool close_only;
    _Atomic bool done;
} DestroyThread;

static void* destroy_thread_main(void* user_data) {
    DestroyThread* d = (DestroyThread*)user_data;
    if (d->close_only) {
        image_system_close(d->sys);
    } else {
        image_system_destroy(d->sys);
    }
    atomic_store(&d->done, true);
    return nullptr;
}

static bool wait_for_entered(GatedBackend* b, int count) {
    int waited = 0;
    while (atomic_load(&b->entered) < count && waited < PREPARE_POLL_TIMEOUT_MS) {
        sleep_ms(1);
        waited++;
    }
    return atomic_load(&b->entered) >= count;
}

#define DESTROY_BLOCKED_PROBE_MS 100

// Starts the destroy (or close) on its own thread while the backend gate is closed, samples whether it finished or
// let a worker finish while gated, then opens the gate and joins.
typedef struct GatedDestroy {
    DestroyThread destroy;
    GatedBackend* backend;
    bool done_while_gated;
    int finished_while_gated;
} GatedDestroy;

static void gated_destroy_run(int* utest_result, GatedDestroy* g) {
    Thread thread;
    ASSERT_TRUE(os_thread_create(&thread, destroy_thread_main, &g->destroy));
    sleep_ms(DESTROY_BLOCKED_PROBE_MS);
    g->done_while_gated = atomic_load(&g->destroy.done);
    g->finished_while_gated = atomic_load(&g->backend->finished);

    atomic_store(&g->backend->gate_open, true);
    os_thread_join(&thread);
}

static const char s_not_an_image[] = "this is not an image";

static bool both_prepares_pending(ImageSystem* sys, FlImageLoadRequestId a, FlImageLoadRequestId b) {
    return image_system_prepare_pending(sys, a) && image_system_prepare_pending(sys, b);
}

UTEST(image_teardown, destroy_waits_for_worker_in_prepare) {
    GatedBackend b = { 0 };
    FlRenderBackend backend = { .user_data = &b, .prepare_image = gated_prepare_image, .prepare_image_on_job = true };
    ImageSystem* sys = image_system_create(s_arena, &backend);

    ASSERT_NE(image_system_load_from_memory(sys, s_svg, sizeof(s_svg) - 1, nullptr, nullptr),
              (FlImageLoadRequestId)FL_IMAGE_LOAD_REQUEST_ID_INVALID);
    ASSERT_TRUE(wait_for_entered(&b, 1));

    GatedDestroy g = { .destroy = { .sys = sys }, .backend = &b };
    gated_destroy_run(utest_result, &g);
    const BoolCase waited[] = {
        { g.done_while_gated, false, "destroy must not return while a worker is in the backend" },
        { g.finished_while_gated == 0, true, "no worker finished while the gate was closed" },
        { atomic_load(&g.destroy.done), true, "destroy returns once the gate opens" },
        { atomic_load(&b.finished) == 1, true, "the worker finished" },
    };
    check_bool_cases(utest_result, waited, sizeof_array(waited));
}

// A burst of more than 64 loads. Every load is tracked, and jobs still queued when teardown begins skip the
// backend, which the caller is about to free.

#define TEARDOWN_BURST 200

UTEST(image_teardown, burst_past_pending_capacity_is_tracked_and_drained) {
    GatedBackend b = { 0 };
    FlRenderBackend backend = { .user_data = &b, .prepare_image = gated_prepare_image, .prepare_image_on_job = true };
    ImageSystem* sys = image_system_create(s_arena, &backend);

    for (int i = 0; i < TEARDOWN_BURST; i++) {
        ASSERT_NE(image_system_load_from_memory(sys, s_svg, sizeof(s_svg) - 1, nullptr, nullptr),
                  (FlImageLoadRequestId)FL_IMAGE_LOAD_REQUEST_ID_INVALID);
    }
    ASSERT_TRUE(wait_for_entered(&b, 1));
    ASSERT_EQ(image_system_pending_job_count(sys), (u32)TEARDOWN_BURST);

    GatedDestroy g = { .destroy = { .sys = sys }, .backend = &b };
    gated_destroy_run(utest_result, &g);
    ASSERT_FALSE(g.done_while_gated);
    ASSERT_TRUE(atomic_load(&g.destroy.done));

    // Only the workers already inside the backend ran. The rest of the burst was skipped, and nothing reaches
    // the backend after destroy returned.
    int entered = atomic_load(&b.entered);
    ASSERT_LT(entered, TEARDOWN_BURST);
    ASSERT_EQ(atomic_load(&b.finished), entered);
    sleep_ms(DESTROY_BLOCKED_PROBE_MS);
    ASSERT_EQ(atomic_load(&b.entered), entered);
}

// A failed decode still reaches the backend and then records its failure. Teardown waits for that record, so the
// failed-load state it writes is not destroyed while it is being written.

UTEST(image_teardown, close_waits_for_failed_decode_in_prepare) {
    GatedBackend b = { 0 };
    FlRenderBackend backend = { .user_data = &b, .prepare_image = gated_prepare_image, .prepare_image_on_job = true };
    ImageSystem* sys = image_system_create(s_arena, &backend);

    FlImageLoadRequestId bad
        = image_system_load_from_memory(sys, s_not_an_image, sizeof(s_not_an_image), nullptr, nullptr);
    ASSERT_NE(bad, (FlImageLoadRequestId)FL_IMAGE_LOAD_REQUEST_ID_INVALID);
    ASSERT_TRUE(wait_for_entered(&b, 1));
    ASSERT_FALSE(image_system_load_failed(sys, bad));

    GatedDestroy g = { .destroy = { .sys = sys, .close_only = true }, .backend = &b };
    gated_destroy_run(utest_result, &g);
    ASSERT_FALSE(g.done_while_gated);
    ASSERT_TRUE(atomic_load(&g.destroy.done));
    ASSERT_TRUE(image_system_load_failed(sys, bad));

    image_system_destroy(sys);
}

// A failed decode on a deferred backend reaches the pump queue with a null result. Teardown discards it, as it
// does a successful one, without calling the backend.

UTEST(image_teardown, deferred_prepares_discarded_without_backend_call) {
    PrepareStats stats = { 0 };
    FlRenderBackend backend
        = { .user_data = &stats, .prepare_image = stub_prepare_image, .prepare_image_on_job = false };
    ImageSystem* sys = image_system_create(s_arena, &backend);

    FlImageLoadRequestId ok = image_system_load_from_memory(sys, s_svg, sizeof(s_svg) - 1, nullptr, nullptr);
    FlImageLoadRequestId bad
        = image_system_load_from_memory(sys, s_not_an_image, sizeof(s_not_an_image), nullptr, nullptr);
    ASSERT_NE(ok, (FlImageLoadRequestId)FL_IMAGE_LOAD_REQUEST_ID_INVALID);
    ASSERT_NE(bad, (FlImageLoadRequestId)FL_IMAGE_LOAD_REQUEST_ID_INVALID);

    int waited = 0;
    while (!both_prepares_pending(sys, ok, bad) && waited < PREPARE_POLL_TIMEOUT_MS) {
        sleep_ms(1);
        waited++;
    }
    ASSERT_TRUE(image_system_prepare_pending(sys, ok));
    ASSERT_TRUE(image_system_prepare_pending(sys, bad));

    image_system_destroy(sys);
    ASSERT_EQ(atomic_load(&stats.call_count), 0);
}

// VFS read callbacks also produce loads: teardown waits for one held in the backend and refuses new VFS loads.

UTEST(image_teardown, destroy_waits_for_vfs_load_in_prepare) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    vfs_wait_all();
    ASSERT_TRUE(fl_vfs_mount_is_ready(mount.mount));

    GatedBackend b = { 0 };
    FlRenderBackend backend = { .user_data = &b, .prepare_image = gated_prepare_image, .prepare_image_on_job = true };
    ImageSystem* sys = image_system_create(s_arena, &backend);

    const ImageLoadSource source = { .mount = mount.mount, .relative_path = FL_S(STUB_VFS_FILE) };
    FlImageLoadRequestId id = image_system_load(sys, &source, FL_IMAGE_LOAD_REQUEST_ID_INVALID);
    ASSERT_NE(id, (FlImageLoadRequestId)FL_IMAGE_LOAD_REQUEST_ID_INVALID);
    ASSERT_TRUE(wait_for_entered(&b, 1));

    GatedDestroy g = { .destroy = { .sys = sys, .close_only = true }, .backend = &b };
    gated_destroy_run(utest_result, &g);
    const BoolCase closed[] = {
        { g.done_while_gated, false, "close must not return while a worker is in the backend" },
        { g.finished_while_gated == 0, true, "no worker finished while the gate was closed" },
        { atomic_load(&g.destroy.done), true, "close returns once the gate opens" },
        { atomic_load(&b.finished) == 1, true, "the worker finished" },
    };
    check_bool_cases(utest_result, closed, sizeof_array(closed));

    const ImageLoadSource blocked_source = { .mount = mount.mount, .relative_path = FL_S(STUB_VFS_FILE) };
    ASSERT_EQ(image_system_load(sys, &blocked_source, FL_IMAGE_LOAD_REQUEST_ID_INVALID),
              (FlImageLoadRequestId)FL_IMAGE_LOAD_REQUEST_ID_INVALID);

    image_system_destroy(sys);
    fl_vfs_mount_close(mount.mount);
}

// Once teardown has begun, new in-memory loads are refused.

UTEST(image_teardown, loads_refused_after_close) {
    PrepareStats stats = { 0 };
    FlRenderBackend backend
        = { .user_data = &stats, .prepare_image = stub_prepare_image, .prepare_image_on_job = true };
    ImageSystem* sys = image_system_create(s_arena, &backend);

    image_system_close(sys);
    ASSERT_EQ(image_system_load_from_memory(sys, s_svg, sizeof(s_svg) - 1, nullptr, nullptr),
              (FlImageLoadRequestId)FL_IMAGE_LOAD_REQUEST_ID_INVALID);
    ASSERT_EQ(atomic_load(&stats.call_count), 0);

    image_system_destroy(sys);
}

// The host destroys its surfaces, shared resources and backend, so it drains the image workers before any of
// that. The surface teardown is the first step of the backend side that the test can see.

static GatedBackend s_host_gate;

static _Atomic bool s_host_surface_destroyed;

static void (*s_host_real_destroy_surface)(void*, FlSurface*);

static bool host_gated_prepare_image(void* user_data, FlImageLoadRequestId request_id,
                                     const FlImageDecodeResult* decoded, FlArena* scratch,
                                     const FlImageLoadRequest* request) {
    (void)user_data;
    return gated_prepare_image(&s_host_gate, request_id, decoded, scratch, request);
}

static void host_recording_destroy_surface(void* user_data, FlSurface* surface) {
    atomic_store(&s_host_surface_destroyed, true);
    s_host_real_destroy_surface(user_data, surface);
}

typedef struct HostDestroyThread {
    FlHost* host;
    _Atomic bool done;
} HostDestroyThread;

static void* host_destroy_thread_main(void* user_data) {
    HostDestroyThread* d = (HostDestroyThread*)user_data;
    host_destroy(d->host);
    atomic_store(&d->done, true);
    return nullptr;
}

UTEST(image_teardown, host_destroy_drains_image_worker_before_backend_teardown) {
    s_host_gate = (GatedBackend) { 0 };
    atomic_store(&s_host_surface_destroyed, false);

    FlRenderBackend backend = create_software_renderer(s_arena, nullptr, FontRenderMode_Grayscale);
    backend.prepare_image = host_gated_prepare_image;
    backend.prepare_image_on_job = true;
    s_host_real_destroy_surface = backend.destroy_surface;
    backend.destroy_surface = host_recording_destroy_surface;

    FlHostSettings settings = { 0 };
    FlHost* host = host_create(s_arena, &settings, &backend);
    ASSERT_TRUE(host != nullptr);
    ASSERT_TRUE(host_surface_create(host, 64, 64) != nullptr);

    FlImageLoadOptions options = { 0 };
    ASSERT_NE(image_load_from_memory(s_svg, sizeof(s_svg) - 1, options), (ImageHandle)IMAGE_HANDLE_INVALID);
    ASSERT_TRUE(wait_for_entered(&s_host_gate, 1));

    HostDestroyThread d = { .host = host };
    Thread thread;
    ASSERT_TRUE(os_thread_create(&thread, host_destroy_thread_main, &d));
    sleep_ms(DESTROY_BLOCKED_PROBE_MS);
    const bool done_while_gated = atomic_load(&d.done);
    const bool surface_destroyed_while_gated = atomic_load(&s_host_surface_destroyed);

    atomic_store(&s_host_gate.gate_open, true);
    os_thread_join(&thread);
    const BoolCase drained[] = {
        { done_while_gated, false, "host_destroy must not return while an image worker is in the backend" },
        { surface_destroyed_while_gated, false, "backend torn down while an image worker was running" },
        { atomic_load(&d.done), true, "host_destroy returns once the gate opens" },
        { atomic_load(&s_host_surface_destroyed), true, "the surface is destroyed after the drain" },
        { atomic_load(&s_host_gate.finished) == 1, true, "the worker finished" },
    };
    check_bool_cases(utest_result, drained, sizeof_array(drained));

    // The adopted backend still belongs to the caller and is released only after the host is gone.
    backend.destroy(backend.user_data);
}

// The dedup cache maps an image to its request id. Freeing the image removes that entry, so loading the same path
// again starts a new request and does not return the freed handle.

UTEST(image_load, free_then_reload_returns_a_working_handle) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    vfs_test_register_driver(&s_stub_vfs_plugin);
    s_stub_vfs_override_data = k_png_red;
    s_stub_vfs_override_size = sizeof(k_png_red);

    FlVfsMountResult mount = vfs_mount(FL_S("/stub-image-reload"));
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    vfs_wait_all();

    const FlImageLoadOptions options = { 0 };
    const FlImageHandle first = fl_image_load_mount(mount.mount, FL_S(STUB_VFS_FILE), options);
    ASSERT_NE(first, FL_IMAGE_HANDLE_INVALID);
    ASSERT_EQ_MSG(fl_image_load_mount(mount.mount, FL_S(STUB_VFS_FILE), options), first,
                  "an identical load is deduplicated while the image lives");
    ASSERT_EQ(wait_public_image_terminal(first), FlImageStatus_Ready);

    image_free((ImageHandle)first);
    const FlImageHandle second = fl_image_load_mount(mount.mount, FL_S(STUB_VFS_FILE), options);
    ASSERT_NE(second, FL_IMAGE_HANDLE_INVALID);
    ASSERT_NE_MSG(second, first, "a freed image must not be served from the dedup cache");
    ASSERT_EQ_MSG(wait_public_image_terminal(second), FlImageStatus_Ready, "the reloaded image must realize");

    image_free((ImageHandle)second);
    fl_vfs_mount_close(mount.mount);
    s_stub_vfs_override_data = nullptr;
    s_stub_vfs_override_size = 0;
}

// The dedup key names the mount instance and not its source path. Two live mounts of one source keep separate
// cache entries, and a remount of a closed source starts with none.

UTEST(image_load, mounts_of_one_source_do_not_share_dedup_entries) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    vfs_test_register_driver(&s_stub_vfs_plugin);
    s_stub_vfs_override_data = k_png_red;
    s_stub_vfs_override_size = sizeof(k_png_red);

    FlVfsMountResult a = vfs_mount(FL_S("/stub-image-twin"));
    FlVfsMountResult b = vfs_mount(FL_S("/stub-image-twin"));
    ASSERT_EQ(a.status, FlVfsMountErrorStatus_Success);
    ASSERT_EQ(b.status, FlVfsMountErrorStatus_Success);
    vfs_wait_all();

    const FlImageLoadOptions options = { 0 };
    const FlImageHandle from_a = fl_image_load_mount(a.mount, FL_S(STUB_VFS_FILE), options);
    const FlImageHandle from_b = fl_image_load_mount(b.mount, FL_S(STUB_VFS_FILE), options);
    const BoolCase handles[] = {
        { from_a != FL_IMAGE_HANDLE_INVALID, true, "the first mount's load starts" },
        { from_b != FL_IMAGE_HANDLE_INVALID, true, "the second mount's load starts" },
        { from_a != from_b, true, "separate mounts of one source must not share a handle" },
    };
    check_bool_cases(utest_result, handles, sizeof_array(handles));
    ASSERT_EQ(wait_public_image_terminal(from_a), FlImageStatus_Ready);
    ASSERT_EQ(wait_public_image_terminal(from_b), FlImageStatus_Ready);

    image_free((ImageHandle)from_a);
    ASSERT_EQ_MSG(fl_image_load_mount(b.mount, FL_S(STUB_VFS_FILE), options), from_b,
                  "freeing one mount's image leaves the other mount's cache entry alone");
    ASSERT_EQ(fl_image_status(from_b), FlImageStatus_Ready);

    image_free((ImageHandle)from_b);
    fl_vfs_mount_close(a.mount);
    fl_vfs_mount_close(b.mount);
    s_stub_vfs_override_data = nullptr;
    s_stub_vfs_override_size = 0;
}

UTEST(image_load, remount_of_a_closed_source_does_not_return_a_stale_failed_entry) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    vfs_test_register_driver(&s_stub_vfs_plugin);
    s_stub_vfs_override_data = nullptr; // the stub's text payload, which is not a decodable image

    FlVfsMountResult first = vfs_mount(FL_S("/stub-image-remount"));
    ASSERT_EQ(first.status, FlVfsMountErrorStatus_Success);
    vfs_wait_all();

    const FlImageLoadOptions options = { 0 };
    const FlImageHandle failed = fl_image_load_mount(first.mount, FL_S(STUB_VFS_FILE), options);
    ASSERT_NE(failed, FL_IMAGE_HANDLE_INVALID);
    ASSERT_EQ(wait_public_image_terminal(failed), FlImageStatus_Error);
    fl_vfs_mount_close(first.mount);

    s_stub_vfs_override_data = k_png_red;
    s_stub_vfs_override_size = sizeof(k_png_red);
    FlVfsMountResult second = vfs_mount(FL_S("/stub-image-remount"));
    ASSERT_EQ(second.status, FlVfsMountErrorStatus_Success);
    vfs_wait_all();

    const FlImageHandle loaded = fl_image_load_mount(second.mount, FL_S(STUB_VFS_FILE), options);
    ASSERT_NE(loaded, FL_IMAGE_HANDLE_INVALID);
    ASSERT_NE_MSG(loaded, failed, "a remounted source must not be served the old mount's failed entry");
    ASSERT_EQ(wait_public_image_terminal(loaded), FlImageStatus_Ready);

    image_free((ImageHandle)failed);
    image_free((ImageHandle)loaded);
    fl_vfs_mount_close(second.mount);
    s_stub_vfs_override_data = nullptr;
    s_stub_vfs_override_size = 0;
}

// Loading and forgetting one path repeatedly leaves no live entries, so the key storage does not grow with the
// cycles.

#define DEDUP_CHURN_CYCLES 2000

UTEST(image_load, dedup_key_storage_stays_bounded_under_load_free_churn) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    vfs_wait_all();

    FlRenderBackend backend = { 0 };
    ImageSystem* sys = image_system_create(s_arena, &backend);
    ASSERT_TRUE(sys != nullptr);

    const ImageDecodeOptions decode_options = { 0 };
    FlImageLoadRequestId* ids = arena_alloc_array(s_arena, FlImageLoadRequestId, DEDUP_CHURN_CYCLES);
    for (int i = 0; i < DEDUP_CHURN_CYCLES; i++) {
        bool was_cached = true;
        const ImageLoadSource source
            = { .mount = mount.mount, .relative_path = FL_S(STUB_VFS_FILE), .options = &decode_options };
        ids[i] = image_system_load_dedup(sys, &source, &was_cached);
        ASSERT_NE(ids[i], (FlImageLoadRequestId)FL_IMAGE_LOAD_REQUEST_ID_INVALID);
        ASSERT_FALSE_MSG(was_cached, "every cycle starts from an empty cache");
        image_system_forget_dedup(sys, ids[i]);
    }
    ASSERT_LT_MSG(image_system_dedup_key_bytes(sys), 32768u, "removed keys must be reclaimed, not retained");

    vfs_wait_all();
    for (int i = 0; i < DEDUP_CHURN_CYCLES; i++) {
        fl_vfs_close(ids[i]);
    }
    image_system_destroy(sys);
    fl_vfs_mount_close(mount.mount);
}
