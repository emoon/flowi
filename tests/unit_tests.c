// The single CPU test executable, built on the utest.h runner. All CPU-side tests live in this directory as
// UTEST cases, one unit_tests_<area>.c per area, sharing tests/unit_tests_common.h. GPU tests live in
// gpu_smoke_test. Do not add another test executable.
//
// This file holds the runner and the process-global lifecycle cases. The runner resets the process singletons
// (render fixture, VFS, image system) before and after every case, so no case depends on its position in the run.
//
// DUMP_FRAMES=1 ./build/unit_tests dumps each rendered frame to frame_dump_<label>.ppm, so a deliberate
// rendering change can be reviewed as an image.

#include "unit_tests_common.h"

// image_free calls the backend's release_image so the GPU backend can reclaim the freed handle's atlas cell or
// slot texture. image_free reaches the global image system, so the cases create it with image_create_system.

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

// The software backend leaves release_image unbound.
UTEST(image_free, unbound_release_seam_is_noop) {
    FlRenderBackend backend = { .user_data = nullptr, .release_image = nullptr };
    image_create_system(s_arena, &backend);
    image_free((ImageHandle)101);
    image_destroy_system();
}

// An fl_app that is the sole owner of the process-global singletons (VFS, image system) resets them on destroy,
// so a second create in the same process initializes them again and does not reuse pointers into the destroyed
// app arena. When another owner already exists, fl_app's create calls are no-ops and destroy skips the teardown.

static void app_cycle_frame(void* user_data) {
    *(bool*)user_data = true;
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        // An empty full-window area is enough to drive the whole tick path.
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

        // Init returns true only when it claims ownership, which shows that destroy reset the globals. Release
        // them at once so the next cycle's fl_app_create is the sole owner again.
        ASSERT_TRUE_MSG(fl_vfs_init(s_arena), "fl_app_destroy left the VFS singleton initialized");
        vfs_destroy();
        FlRenderBackend dummy = { 0 };
        ASSERT_TRUE_MSG(image_create_system(s_arena, &dummy), "fl_app_destroy left the image system initialized");
        image_destroy_system();
    }
}

UTEST_STATE();

// Each registered case is replaced by run_isolated_case, which keeps the original function and index in a side
// table addressed through the case's own index slot.
typedef struct IsolatedCase {
    utest_testcase_t func;
    size_t index;
} IsolatedCase;

static IsolatedCase* s_isolated_cases;

static void run_isolated_case(int* result, size_t slot) {
    const IsolatedCase wrapped = s_isolated_cases[slot];
    process_singletons_reset();
    wrapped.func(result, wrapped.index);
    process_singletons_reset();
}

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

int main(int argc, const char* const argv[]) {
    s_arena = fl_init(1); // one job worker thread, separate from the main test thread
    if (!s_arena || !isolate_cases()) {
        return 1;
    }
    return utest_main(argc, argv);
}
