// VFS teardown tests: destroying an owner of a queued read waits for the read

#include "vfs_test_support.h"

#include "ui/localization.h"

// A destroy that waits for a queued read cannot return until the parked worker is released, so something other
// than the destroying thread has to release it. The releaser does so once destroy_returned says destroy came
// back. A destroy that waits can never set that, so the releaser's deadline lets it through. A destroy that does
// not wait is back long before the deadline.

typedef struct VfsTeardownCtx {
    VfsParkCtx* park;
    _Atomic bool destroy_returned;
} VfsTeardownCtx;

static bool teardown_destroy_returned(const void* arg) {
    return atomic_load(&((const VfsTeardownCtx*)arg)->destroy_returned);
}

static void* vfs_teardown_releaser(void* user_data) {
    VfsTeardownCtx* ctx = (VfsTeardownCtx*)user_data;
    (void)vfs_await_until(teardown_destroy_returned, ctx, 100000000ull);
    atomic_store(&ctx->park->release, true);
    return nullptr;
}

// Starts the releaser. If that fails the worker is released here, so the destroy under test cannot block forever.
static bool vfs_teardown_begin(VfsTeardownCtx* ctx, VfsParkCtx* park, Thread* releaser) {
    ctx->park = park;
    atomic_init(&ctx->destroy_returned, false);
    if (os_thread_create(releaser, vfs_teardown_releaser, ctx)) {
        return true;
    }
    atomic_store(&park->release, true);
    return false;
}

// Call right after the destroy under test returns. True if the parked worker had been released by then, which
// happens only if the destroy waited for the queued read.
static bool vfs_teardown_end(VfsTeardownCtx* ctx, bool releasing, Thread* releaser) {
    bool waited = atomic_load(&ctx->park->release);
    atomic_store(&ctx->destroy_returned, true);
    if (releasing) {
        os_thread_join(releaser);
    }
    return waited;
}

typedef struct TeardownFindings {
    bool parked;
    bool queued;
    bool releasing;
    bool waited;
} TeardownFindings;

// Nothing may ASSERT while the worker is parked. See vfs_park_worker.
static void teardown_run(TeardownFindings* found, VfsParkCtx* park, void (*destroy)(void*), void* subject) {
    VfsTeardownCtx teardown;
    Thread releaser = { 0 };
    found->releasing = vfs_teardown_begin(&teardown, park, &releaser);
    destroy(subject);
    found->waited = vfs_teardown_end(&teardown, found->releasing, &releaser);
}

static bool teardown_waited_for_the_read(const TeardownFindings* found, const char* queued_what,
                                         const char* waited_what) {
    return vfs_check(found->parked, "the parking callback never ran") && vfs_check(found->queued, queued_what)
           && vfs_check(found->releasing, "the releaser thread must start; the destroy under test blocks without it")
           && vfs_check(found->waited, waited_what);
}

static void destroy_font_cache(void* cache) {
    font_cache_destroy(cache);
}

static void destroy_localization(void* loc) {
    localization_destroy(loc);
}

static void teardown_release_parker(FlVfsMount* mount, FlVfsHandle parker) {
    fl_vfs_wait(parker);
    vfs_wait_all();
    fl_vfs_close(parker);
    fl_vfs_mount_close(mount);
}

// font_cache_destroy frees the arena a font load's callback writes into. fl_vfs_close closes the load but does
// not stop it, so the destroy waits for the queued read first. The stub bytes are not a font, so the callback
// fails after it has touched the face and its context.

UTEST(vfs, font_cache_destroy_waits_for_a_queued_face_load) {
    FlVfsMount* mount = stub_mount_ready();
    ASSERT_TRUE_MSG(mount != nullptr, "stub mount should succeed");

    VfsParkCtx park;
    FlVfsHandle parker;
    TeardownFindings found = { .parked = vfs_park_worker(&park, mount, &parker, FL_S(STUB_VFS_FILE)) };

    FlFontCache* cache = font_cache_new(s_arena);
    FlFontId font = font_cache_load_font(cache, mount, FL_S(STUB_VFS_FILE), 16); // queued behind parker
    found.queued = font != FONT_ID_INVALID;

    teardown_run(&found, &park, destroy_font_cache, cache);

    // A destroy that skipped the wait leaves the load to run against freed memory once the worker is free.
    teardown_release_parker(mount, parker);

    ASSERT_TRUE(teardown_waited_for_the_read(
        &found, "the face load must have been queued",
        "font_cache_destroy must not return before the queued face load has been released to run"));
}

// localization_destroy frees the arena the manifest read parses into, so it has to wait for a read still queued
// at startup. The stub bytes are not JSON, so the parse fails after it has allocated from the manifest's arena.

UTEST(vfs, localization_destroy_waits_for_a_queued_manifest_read) {
    FlVfsMount* mount = stub_mount_ready();
    ASSERT_TRUE_MSG(mount != nullptr, "stub mount should succeed");

    VfsParkCtx park;
    FlVfsHandle parker;
    TeardownFindings found = { .parked = vfs_park_worker(&park, mount, &parker, FL_S(STUB_VFS_FILE)) };

    struct Localization* loc = localization_create(mount, FL_S(STUB_VFS_FILE), false); // queued behind parker
    found.queued = localization_is_loading(loc);

    teardown_run(&found, &park, destroy_localization, loc);

    teardown_release_parker(mount, parker);

    ASSERT_TRUE(teardown_waited_for_the_read(
        &found, "the manifest read must still be queued when destroy runs",
        "localization_destroy must not return before the queued manifest read has been released"));
}

// The same for a string table read: the manifest is served as JSON that lists the stub file as the default language's
// table, so the frame boundary that finishes the manifest queues a table read behind the parked worker.

UTEST(vfs, localization_destroy_waits_for_a_queued_table_read) {
    static const char k_manifest[] = "{ default: 'en', languages: [{ tag: 'en', file: 'hello.txt' }] }";

    FlVfsMount* mount = stub_mount_ready();
    ASSERT_TRUE_MSG(mount != nullptr, "stub mount should succeed");

    s_stub_vfs_override_data = (const u8*)k_manifest;
    s_stub_vfs_override_size = sizeof(k_manifest) - 1;
    struct Localization* loc = localization_create(mount, FL_S(STUB_VFS_FILE), false);
    vfs_wait_all(); // the manifest read is done; the next frame boundary starts the table read

    VfsParkCtx park;
    FlVfsHandle parker;
    TeardownFindings found = { .parked = vfs_park_worker(&park, mount, &parker, FL_S(STUB_VFS_FILE)) };

    localization_frame_begin(loc); // table queued behind parker
    found.queued = localization_is_loading(loc);

    teardown_run(&found, &park, destroy_localization, loc);

    fl_vfs_wait(parker);
    vfs_wait_all();
    s_stub_vfs_override_data = nullptr;
    s_stub_vfs_override_size = 0;
    fl_vfs_close(parker);
    fl_vfs_mount_close(mount);

    ASSERT_TRUE(teardown_waited_for_the_read(
        &found, "the table read must still be queued when destroy runs",
        "localization_destroy must not return before the queued table read has been released"));
}
