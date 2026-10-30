// Unit tests (utest.h runner) - the single CPU test executable. All CPU-side tests live in this
// directory as UTEST cases, one unit_tests_<area>.c per area, sharing tests/unit_tests_common.h;
// GPU tests live in gpu_smoke_test. Never add a new test executable.
//
// This TU carries only the runner plus the process-global lifecycle cases. The runner resets the process
// singletons (render fixture, VFS, image system) before and after every case, so no case depends on its position
// in the run.
//
// DUMP_FRAMES=1 ./build/unit_tests dumps each rendered frame to frame_dump_<label>.ppm, so a deliberate
// rendering change can be reviewed as an image.

#include "unit_tests_common.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// image_free must drive the backend's release_image seam so the GPU backend can reclaim the freed handle's
// atlas cell / slot texture. Uses the global image system (image_create_system), which image_free reaches.

typedef struct ReleaseStats {
    int call_count;
    FlImageHandle last_handle;
} ReleaseStats;

static void stub_release_image(void* user_data, FlImageHandle handle) {
    ReleaseStats* stats = (ReleaseStats*)user_data;
    stats->last_handle = handle;
    stats->call_count++;
}

UTEST(image_free, forwards_freed_handle_to_backend_release) {
    ReleaseStats stats = { 0 };
    FlRenderBackend backend = {
        .user_data = &stats,
        .release_image = stub_release_image,
    };
    image_create_system(s_arena, &backend);

    image_free((ImageHandle)101);
    ASSERT_EQ(stats.call_count, 1);
    ASSERT_EQ(stats.last_handle, (FlImageHandle)101);

    image_destroy_system();
}

// SW leaves release_image unbound; image_free must null-guard and not crash.
UTEST(image_free, unbound_release_seam_is_noop) {
    FlRenderBackend backend = { .user_data = nullptr, .release_image = nullptr };
    image_create_system(s_arena, &backend);
    image_free((ImageHandle)101); // must not crash
    image_destroy_system();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// fl_app create/destroy/create - a sole-owner fl_app must reset the process-global singletons (VFS, image
// system) on destroy so a second create in the same process re-initializes them instead of reusing pointers
// into the destroyed app arena. Only a sole-owner fl_app exercises the owner-gated teardown path - after another
// owner exists, fl_app's create calls are no-ops and destroy skips.

static void app_cycle_frame(void* user_data) {
    *(bool*)user_data = true;
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        // Empty full-window area: enough to drive the whole tick path.
    }
}

UTEST(fl_app, create_destroy_create_reinitializes_global_singletons) {
    for (int cycle = 0; cycle < 2; cycle++) {
        FlAppSettings settings = { .window_title = FL_S("cycle"), .width = 320, .height = 200, .headless = true };
        FlApp* app = fl_app_create(&settings);
        ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed in cycle");
        bool frame_ran = false;
        fl_app_tick(app, &frame_ran, app_cycle_frame);
        ASSERT_TRUE_MSG(frame_ran, "frame did not run in cycle");
        fl_app_destroy(app);

        // Owner-gated teardown proof: destroy reset the globals, so a fresh init claims ownership (returns
        // true). Release immediately so the next cycle's fl_app_create is again the sole owner.
        ASSERT_TRUE_MSG(fl_vfs_init(s_arena), "fl_app_destroy left the VFS singleton initialized");
        vfs_destroy();
        FlRenderBackend dummy = { 0 };
        ASSERT_TRUE_MSG(image_create_system(s_arena, &dummy), "fl_app_destroy left the image system initialized");
        image_destroy_system();
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_STATE();

// Each registered case is replaced by run_isolated_case, which keeps the original function and index in a side
// table addressed through the case's own index slot.
typedef struct IsolatedCase {
    utest_testcase_t func;
    size_t index;
} IsolatedCase;

static IsolatedCase* s_isolated_cases;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void run_isolated_case(int* result, size_t slot) {
    const IsolatedCase wrapped = s_isolated_cases[slot];
    process_singletons_reset();
    wrapped.func(result, wrapped.index);
    process_singletons_reset();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool isolate_cases(void) {
    s_isolated_cases = arena_alloc_array(s_arena, IsolatedCase, utest_state.tests_length);
    if (!s_isolated_cases) {
        return false;
    }
    for (size_t i = 0; i < utest_state.tests_length; i++) {
        s_isolated_cases[i] = (IsolatedCase) { utest_state.tests[i].func, utest_state.tests[i].index };
        utest_state.tests[i].func = run_isolated_case;
        utest_state.tests[i].index = i;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, const char* const argv[]) {
    s_arena = fl_init(1); // one job worker thread, distinct from the (main) test thread
    if (!s_arena || !isolate_cases()) {
        return 1;
    }
    return utest_main(argc, argv);
}
