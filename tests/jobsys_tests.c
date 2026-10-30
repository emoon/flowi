#include "utest/utest.h"
#include <core/arena.h>
#include <core/core.h>
#include <core/jobsys.h>
#include <core/os/os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    _Atomic u32 counter;
    int expected_increments;
} TestData;

typedef struct {
    int delay_ms;
    int job_id;
} DelayedJob;

static _Atomic u32 global_counter = 0;

static void simple_job(void* data, FlJobsWorkerInfo info) {
    (void)info;
    int* value = (int*)data;
    (*value)++;
}

static void increment_counter(void* data, FlJobsWorkerInfo info) {
    (void)info;
    TestData* test_data = (TestData*)data;
    atomic_fetch_add(&test_data->counter, 1);
}

static void delayed_job(void* data, FlJobsWorkerInfo info) {
    (void)info;
    DelayedJob* job = (DelayedJob*)data;
    sleep_us(job->delay_ms * 1000);
    atomic_fetch_add(&global_counter, 1);
}

typedef struct {
    _Atomic bool release;
} GatedJob;

// Runs until the test releases it, so a case that needs a job to still be in flight states that as an
// invariant rather than betting the job outlasts a sleep - a loaded CI host hands a 50 ms sleep back whenever
// it feels like it.
static void gated_job(void* data, FlJobsWorkerInfo info) {
    (void)info;
    GatedJob* gate = (GatedJob*)data;
    while (!atomic_load(&gate->release)) {
        sleep_us(200);
    }
    atomic_fetch_add(&global_counter, 1);
}

UNUSED_FUNCTION static void cpu_intensive_job(void* data, FlJobsWorkerInfo info) {
    (void)info;
    int* iterations = (int*)data;
    volatile long sum = 0;
    (void)sum; // Unused but intentional for CPU-intensive work
    for (int i = 0; i < *iterations; i++) {
        sum += i;
    }
    atomic_fetch_add(&global_counter, 1);
}

typedef struct {
    _Atomic u32 execution_order[10];
    _Atomic u32 next_order;
} DependencyTestData;

static void ordered_job_a(void* data, FlJobsWorkerInfo info) {
    (void)info;
    DependencyTestData* test_data = (DependencyTestData*)data;
    int order = atomic_fetch_add(&test_data->next_order, 1);
    atomic_store(&test_data->execution_order[0], order);
}

static void ordered_job_b(void* data, FlJobsWorkerInfo info) {
    (void)info;
    DependencyTestData* test_data = (DependencyTestData*)data;
    int order = atomic_fetch_add(&test_data->next_order, 1);
    atomic_store(&test_data->execution_order[1], order);
}

static void ordered_job_c(void* data, FlJobsWorkerInfo info) {
    (void)info;
    DependencyTestData* test_data = (DependencyTestData*)data;
    int order = atomic_fetch_add(&test_data->next_order, 1);
    atomic_store(&test_data->execution_order[2], order);
}

static void slow_job(void* data, FlJobsWorkerInfo info) {
    (void)info;
    DependencyTestData* test_data = (DependencyTestData*)data;
    sleep_us(100000); // 100ms delay
    int order = atomic_fetch_add(&test_data->next_order, 1);
    atomic_store(&test_data->execution_order[0], order);
}

typedef struct {
    _Atomic u32 completion_index;
    u32* completion_order;
    u32 max_jobs;
    JobPriority priority;
} PriorityTestData;

typedef struct {
    _Atomic u32 high_count;
    _Atomic u32 normal_count;
    _Atomic u32 low_count;
    _Atomic u32 total_completed;
    bool high_should_be_done;
    bool normal_should_be_done;
} PriorityOrderData;

static void priority_order_job(void* data, FlJobsWorkerInfo info) {
    (void)info;
    PriorityOrderData* test_data = (PriorityOrderData*)data;

    volatile int sum = 0;
    for (int i = 0; i < 1000; i++) {
        sum += i;
    }
    (void)sum;

    atomic_fetch_add(&test_data->total_completed, 1);
}

static void priority_high_job(void* data, FlJobsWorkerInfo info) {
    (void)info;
    PriorityOrderData* test_data = (PriorityOrderData*)data;

    volatile int sum = 0;
    for (int i = 0; i < 500; i++) {
        sum += i;
    }
    (void)sum;

    atomic_fetch_add(&test_data->high_count, 1);
    atomic_fetch_add(&test_data->total_completed, 1);
}

static void priority_normal_job(void* data, FlJobsWorkerInfo info) {
    (void)info;
    PriorityOrderData* test_data = (PriorityOrderData*)data;

    volatile int sum = 0;
    for (int i = 0; i < 500; i++) {
        sum += i;
    }
    (void)sum;

    atomic_fetch_add(&test_data->normal_count, 1);
    atomic_fetch_add(&test_data->total_completed, 1);
}

static void priority_low_job(void* data, FlJobsWorkerInfo info) {
    (void)info;
    PriorityOrderData* test_data = (PriorityOrderData*)data;

    volatile int sum = 0;
    for (int i = 0; i < 500; i++) {
        sum += i;
    }
    (void)sum;

    atomic_fetch_add(&test_data->low_count, 1);
    atomic_fetch_add(&test_data->total_completed, 1);
}

typedef struct {
    _Atomic u32 high_completions_before_low_start;
    _Atomic u32 high_completed;
    _Atomic u32 low_started;
    _Atomic u32 total_low;
    _Atomic u32 total_high;
} PriorityOrderValidationData;

static void priority_validation_high_job(void* data, FlJobsWorkerInfo info) {
    (void)info;
    PriorityOrderValidationData* d = (PriorityOrderValidationData*)data;

    volatile int sum = 0;
    for (int i = 0; i < 2000; i++) {
        sum += i;
    }
    (void)sum;

    atomic_fetch_add(&d->high_completed, 1);
    atomic_fetch_add(&d->total_high, 1);
}

static void priority_validation_low_job(void* data, FlJobsWorkerInfo info) {
    (void)info;
    PriorityOrderValidationData* d = (PriorityOrderValidationData*)data;

    u32 high_done = atomic_load(&d->high_completed);
    atomic_fetch_add(&d->high_completions_before_low_start, high_done);
    atomic_fetch_add(&d->low_started, 1);

    volatile int sum = 0;
    for (int i = 0; i < 2000; i++) {
        sum += i;
    }
    (void)sum;

    atomic_fetch_add(&d->total_low, 1);
}

typedef struct {
    _Atomic u32 completed;
    FlJobHandle* handles;
    int num_handles;
} ReprioritizeTestData;

static void reprioritize_job(void* data, FlJobsWorkerInfo info) {
    (void)info;
    ReprioritizeTestData* test_data = (ReprioritizeTestData*)data;

    volatile int sum = 0;
    for (int i = 0; i < 1000; i++) {
        sum += i;
    }
    (void)sum;

    atomic_fetch_add(&test_data->completed, 1);
}

typedef struct {
    bool main_thread_result;
    bool worker_thread_result;
} ThreadCheckData;

static void check_thread_context(void* data, FlJobsWorkerInfo info) {
    (void)info;
    ThreadCheckData* check_data = (ThreadCheckData*)data;
    check_data->worker_thread_result = fl_jobs_is_main_thread();
}

// =====================================================================================================
// FIXTURES - Grouped by worker thread count
// =====================================================================================================

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Swap only the global job system: fl_destroy() per test would tear down the one fl_init() runtime
// (main arena, perf_scope, error_report, arena_watchdog) that the whole unit_tests process shares.

static FlArena* jobs_fixture_setup(int num_threads) {
    fl_jobs_destroy();
    FlArena* arena = arena_new();
    fl_jobs_create(arena, num_threads);
    return arena;
}

static void jobs_fixture_teardown(FlArena* arena) {
    fl_jobs_destroy();
    fl_jobs_create(arena, 1); // restore the shared 1-worker system for subsequent tests
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct jobs_1t {
    FlArena* arena;
};

UTEST_F_SETUP(jobs_1t) {
    (void)utest_result;
    utest_fixture->arena = jobs_fixture_setup(1);
}

UTEST_F_TEARDOWN(jobs_1t) {
    (void)utest_result;
    jobs_fixture_teardown(utest_fixture->arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct jobs_2t {
    FlArena* arena;
};

UTEST_F_SETUP(jobs_2t) {
    (void)utest_result;
    utest_fixture->arena = jobs_fixture_setup(2);
}

UTEST_F_TEARDOWN(jobs_2t) {
    (void)utest_result;
    jobs_fixture_teardown(utest_fixture->arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct jobs_4t {
    FlArena* arena;
};

UTEST_F_SETUP(jobs_4t) {
    (void)utest_result;
    utest_fixture->arena = jobs_fixture_setup(4);
}

UTEST_F_TEARDOWN(jobs_4t) {
    (void)utest_result;
    jobs_fixture_teardown(utest_fixture->arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct jobs_8t {
    FlArena* arena;
};

UTEST_F_SETUP(jobs_8t) {
    (void)utest_result;
    utest_fixture->arena = jobs_fixture_setup(8);
}

UTEST_F_TEARDOWN(jobs_8t) {
    (void)utest_result;
    jobs_fixture_teardown(utest_fixture->arena);
}

// =====================================================================================================
// 1-THREAD TESTS
// =====================================================================================================

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(jobs_1t, basic_functionality) {
    (void)utest_fixture;

    int value = 0;
    FlJobHandle handle = fl_jobs_add_job(simple_job, &value);

    fl_jobs_wait(handle);
    ASSERT_EQ(value, 1);
    ASSERT_EQ((int)fl_jobs_is_finished(handle), (int)FlJobsResult_Finished);
}

// =====================================================================================================
// 2-THREAD TESTS
// =====================================================================================================

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(jobs_2t, job_handle_validity) {
    (void)utest_fixture;

    int value = 0;

    FlJobHandle handle = fl_jobs_add_job(simple_job, &value);
    ASSERT_TRUE(handle != (FlJobHandle)-1);

    fl_jobs_wait(handle);
    ASSERT_EQ((int)fl_jobs_is_finished(handle), (int)FlJobsResult_Finished);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(jobs_2t, is_main_thread) {
    (void)utest_fixture;

    ThreadCheckData check_data = { .main_thread_result = false, .worker_thread_result = true };

    check_data.main_thread_result = fl_jobs_is_main_thread();

    FlJobHandle job = fl_jobs_add_job(check_thread_context, &check_data);
    fl_jobs_wait(job);

    ASSERT_TRUE(check_data.main_thread_result);
    ASSERT_FALSE(check_data.worker_thread_result);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(jobs_2t, priority_get_priority) {
    (void)utest_fixture;

    PriorityOrderData test_data = { 0 };

    FlJobHandle high = fl_jobs_add_job_with_priority(priority_high_job, &test_data, JobPriority_High);
    FlJobHandle normal = fl_jobs_add_job_with_priority(priority_normal_job, &test_data, JobPriority_Normal);
    FlJobHandle low = fl_jobs_add_job_with_priority(priority_low_job, &test_data, JobPriority_Low);

    ASSERT_EQ(fl_jobs_get_priority(high), (int)JobPriority_High);
    ASSERT_EQ(fl_jobs_get_priority(normal), (int)JobPriority_Normal);
    ASSERT_EQ(fl_jobs_get_priority(low), (int)JobPriority_Low);

    ASSERT_EQ((int)fl_jobs_get_priority(0xDEADBEEF), (int)FlJobPriority_Invalid);

    FlJobHandle invalid = fl_jobs_add_job_with_priority(priority_high_job, &test_data, FlJobPriority_Invalid);
    ASSERT_EQ(fl_jobs_get_priority(invalid), (int)JobPriority_High);

    fl_jobs_wait(high);
    fl_jobs_wait(normal);
    fl_jobs_wait(low);
    fl_jobs_wait(invalid);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(jobs_2t, priority_set_priority) {
    (void)utest_fixture;

    // One gated blocker per worker, so no worker is free to start the job before its priority changes
    GatedJob blocker_data = { 0 };
    GatedJob second_blocker_data = { 0 };
    FlJobHandle blocker = fl_jobs_add_job(gated_job, &blocker_data);
    FlJobHandle second_blocker = fl_jobs_add_job(gated_job, &second_blocker_data);

    PriorityOrderData test_data = { 0 };

    FlJobHandle job = fl_jobs_add_job_with_priority(priority_low_job, &test_data, JobPriority_Low);
    const int low_priority = fl_jobs_get_priority(job);

    const bool changed = fl_jobs_set_priority(job, JobPriority_High);
    const int high_priority = fl_jobs_get_priority(job);

    atomic_store(&blocker_data.release, true);
    atomic_store(&second_blocker_data.release, true);
    fl_jobs_wait(blocker);
    fl_jobs_wait(second_blocker);
    fl_jobs_wait(job);

    ASSERT_EQ(low_priority, (int)JobPriority_Low);
    ASSERT_TRUE(changed);
    ASSERT_EQ(high_priority, (int)JobPriority_High);
    ASSERT_EQ(atomic_load(&test_data.low_count), 1u);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(jobs_2t, priority_set_on_completed) {
    (void)utest_fixture;

    PriorityOrderData test_data = { 0 };

    FlJobHandle job = fl_jobs_add_job_with_priority(priority_high_job, &test_data, JobPriority_High);

    fl_jobs_wait(job);

    bool changed = fl_jobs_set_priority(job, JobPriority_Low);
    ASSERT_FALSE(changed);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A dependent submitted from a worker must not run before the job it was ordered after. Two workers make that
// observable: the gate occupies one for as long as the test holds it, pinning the dependency as unfinished
// while the other worker submits against it.

typedef struct WorkerDepCtx {
    _Atomic bool gate_started;
    _Atomic bool gate_release;
    _Atomic bool gate_finished;
    _Atomic bool dependent_saw_gate_finished;
    _Atomic u32 dependent_runs;
    _Atomic u64 dependent_handle;
    FlJobHandle dependency;
} WorkerDepCtx;

static void worker_dep_gate_job(void* data, FlJobsWorkerInfo info) {
    (void)info;
    WorkerDepCtx* ctx = (WorkerDepCtx*)data;
    atomic_store(&ctx->gate_started, true);
    while (!atomic_load(&ctx->gate_release)) {
        sleep_us(200);
    }
    // The gate's last act, so a dependent that ran inline reads false here and one promoted afterwards
    // reads true.
    atomic_store(&ctx->gate_finished, true);
}

static void worker_dep_dependent_job(void* data, FlJobsWorkerInfo info) {
    (void)info;
    WorkerDepCtx* ctx = (WorkerDepCtx*)data;
    atomic_store(&ctx->dependent_saw_gate_finished, atomic_load(&ctx->gate_finished));
    atomic_fetch_add(&ctx->dependent_runs, 1);
}

static void worker_dep_submit_job(void* data, FlJobsWorkerInfo info) {
    (void)info;
    WorkerDepCtx* ctx = (WorkerDepCtx*)data;
    FlJobHandle dependent = fl_jobs_add_job_with_dependency(worker_dep_dependent_job, ctx, ctx->dependency);
    atomic_store(&ctx->dependent_handle, dependent);
}

UTEST_F(jobs_2t, worker_submitted_dependent_waits_for_its_dependency) {
    (void)utest_fixture;

    // Static storage, and not one ASSERT until the gate has been released: an ASSERT returns from the case,
    // and a gate left spinning on its worker hangs the fixture teardown's thread join. Everything up to the
    // release records its outcome in a local instead, and the release itself is unconditional.
    static WorkerDepCtx ctx;
    atomic_store(&ctx.gate_started, false);
    atomic_store(&ctx.gate_release, false);
    atomic_store(&ctx.gate_finished, false);
    atomic_store(&ctx.dependent_saw_gate_finished, false);
    atomic_store(&ctx.dependent_runs, 0u);
    atomic_store(&ctx.dependent_handle, 0ull);

    ctx.dependency = fl_jobs_add_job(worker_dep_gate_job, &ctx);

    bool gate_running = ctx.dependency != 0;
    if (gate_running) {
        u64 deadline_ns = get_monotonic_time_ns_os() + 5000000000ull;
        while (!atomic_load(&ctx.gate_started)) {
            if (get_monotonic_time_ns_os() >= deadline_ns) {
                gate_running = false;
                break;
            }
        }
    }

    FlJobHandle submitter = 0;
    FlJobHandle dependent = 0;
    u32 runs_while_gated = 0;
    bool pending_while_gated = false;

    if (gate_running) {
        // The gate holds the first worker, so this one runs on the second and submits from there.
        submitter = fl_jobs_add_job(worker_dep_submit_job, &ctx);
        fl_jobs_wait(submitter);

        // Read while the dependency is provably unfinished: the dependent cannot have run, and its handle
        // must be a genuinely pending one rather than the sentinel an inline execution hands back.
        runs_while_gated = atomic_load(&ctx.dependent_runs);
        dependent = (FlJobHandle)atomic_load(&ctx.dependent_handle);
        pending_while_gated = fl_jobs_is_finished(dependent) == FlJobsResult_NotFinished;
    }

    atomic_store(&ctx.gate_release, true);
    fl_jobs_wait(ctx.dependency);
    fl_jobs_wait(dependent);

    ASSERT_TRUE_MSG(ctx.dependency != 0, "the gate job was never scheduled");
    ASSERT_TRUE_MSG(gate_running, "the gate job never started");
    ASSERT_TRUE(submitter != 0);
    ASSERT_TRUE(dependent != 0);
    ASSERT_EQ(runs_while_gated, 0u);
    ASSERT_TRUE_MSG(pending_while_gated, "the dependent was already complete while its dependency still ran");
    ASSERT_EQ(atomic_load(&ctx.dependent_runs), 1u);
    ASSERT_TRUE_MSG(atomic_load(&ctx.dependent_saw_gate_finished),
                    "the dependent ran before the dependency it was ordered after");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Verify that calling fl_jobs_is_finished() recycles the job slot for reuse.
// Without this call, completed jobs leak their JobInfo allocation permanently.

UTEST_F(jobs_2t, is_finished_recycles_job_slot) {
    (void)utest_fixture;

    int value = 0;

    FlJobHandle handle1 = fl_jobs_add_job(simple_job, &value);
    fl_jobs_wait(handle1);
    ASSERT_EQ(value, 1);

    ASSERT_EQ((int)fl_jobs_is_finished(handle1), (int)FlJobsResult_Finished);

    // Submit another job - it should reuse the recycled slot,
    // which increments the generation and invalidates the old handle
    value = 0;
    FlJobHandle handle2 = fl_jobs_add_job(simple_job, &value);
    fl_jobs_wait(handle2);
    ASSERT_EQ(value, 1);

    ASSERT_EQ((int)fl_jobs_is_finished(handle1), (int)FlJobsResult_Invalid);
    ASSERT_EQ((int)fl_jobs_is_finished(handle2), (int)FlJobsResult_Finished);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Regression: a handle whose job has completed must never answer NotFinished, no matter how often its slot is
// recycled underneath it. fl_jobs_is_finished reads the slot's generation and status as two separate loads; a
// status load that straddles a recycle used to describe the slot's new occupant, so a long-finished handle
// could report NotFinished.

#define STALE_HANDLE_THREADS 6
#define STALE_HANDLE_TARGET_CYCLES 40000

typedef struct StaleHandleCtx {
    _Atomic bool stop;
    _Atomic u32 cycles;
    _Atomic bool saw_not_finished;
} StaleHandleCtx;

// The window is one-shot per handle: it opens when the handle's job completes and its slot goes back on the
// free list, and closes for good once some allocation bumps the generation (after which the handle answers a
// stable Invalid). Busy-poll rather than fl_jobs_wait, whose 100us sleep would throttle the loop far below
// the rate needed to land inside the window.
static void* stale_handle_race_thread(void* user_data) {
    StaleHandleCtx* ctx = (StaleHandleCtx*)user_data;
    int value = 0;
    while (!atomic_load(&ctx->stop) && !atomic_load(&ctx->saw_not_finished)) {
        FlJobHandle handle = fl_jobs_add_job(simple_job, &value);
        while (fl_jobs_is_finished(handle) == FlJobsResult_NotFinished) {
            // Spin until done; the call that observes completion also frees the slot to the free list
        }
        // A peer may claim that slot at any instant from here. Re-querying the same handle must answer
        // Finished or Invalid - never NotFinished, which would be the new occupant's status leaking out.
        if (fl_jobs_is_finished(handle) == FlJobsResult_NotFinished) {
            atomic_store(&ctx->saw_not_finished, true);
        }
        atomic_fetch_add(&ctx->cycles, 1);
    }
    return nullptr;
}

UTEST_F(jobs_8t, stale_handle_never_reports_not_finished) {
    (void)utest_fixture;

    StaleHandleCtx ctx = { 0 };

    // Oversubscribe the fixture's workers so threads get preempted mid-query and actually straddle a
    // recycle instead of always winning both loads before one can land.
    Thread threads[STALE_HANDLE_THREADS];
    for (int i = 0; i < STALE_HANDLE_THREADS; i++) {
        ASSERT_TRUE(os_thread_create(&threads[i], stale_handle_race_thread, &ctx));
    }

    while (atomic_load(&ctx.cycles) < STALE_HANDLE_TARGET_CYCLES && !atomic_load(&ctx.saw_not_finished)) {
        sleep_ms(1);
    }

    // Stop and join before asserting: an early return would leave threads running against a stack context.
    atomic_store(&ctx.stop, true);
    for (int i = 0; i < STALE_HANDLE_THREADS; i++) {
        os_thread_join(&threads[i]);
    }

    ASSERT_GT((int)atomic_load(&ctx.cycles), 0);
    ASSERT_FALSE_MSG(atomic_load(&ctx.saw_not_finished),
                     "a completed job's handle reported NotFinished after its slot was recycled");
}

// =====================================================================================================
// 4-THREAD TESTS
// =====================================================================================================

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(jobs_4t, multiple_jobs) {
    (void)utest_fixture;
    TestData test_data = { .counter = 0, .expected_increments = 100 };

    FlJobHandle handles[100];
    for (int i = 0; i < 100; i++) {
        handles[i] = fl_jobs_add_job(increment_counter, &test_data);
    }

    for (int i = 0; i < 100; i++) {
        fl_jobs_wait(handles[i]);
    }

    ASSERT_EQ((int)atomic_load(&test_data.counter), 100);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(jobs_4t, global_system) {
    (void)utest_fixture;

    TestData test_data = { .counter = 0, .expected_increments = 50 };

    FlJobHandle handles[50];

    for (int i = 0; i < 50; i++) {
        handles[i] = fl_jobs_add_job(increment_counter, &test_data);
    }

    for (int i = 0; i < 50; i++) {
        fl_jobs_wait(handles[i]);
        ASSERT_EQ((int)fl_jobs_is_finished(handles[i]), (int)FlJobsResult_Finished);
    }

    ASSERT_EQ((int)atomic_load(&test_data.counter), 50);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(jobs_4t, is_finished_functionality) {
    (void)utest_fixture;

    GatedJob gate = { .release = false };
    FlJobHandle handle = fl_jobs_add_job(gated_job, &gate);

    ASSERT_EQ((int)fl_jobs_is_finished(handle), (int)FlJobsResult_NotFinished);

    atomic_store(&gate.release, true);
    fl_jobs_wait(handle);
    ASSERT_EQ((int)fl_jobs_is_finished(handle), (int)FlJobsResult_Finished);

    // One job per worker in the fixture.
    GatedJob gates[4] = { 0 };
    FlJobHandle handles[4];
    for (int i = 0; i < 4; i++) {
        handles[i] = fl_jobs_add_job(gated_job, &gates[i]);
    }

    for (int i = 0; i < 4; i++) {
        ASSERT_EQ((int)fl_jobs_is_finished(handles[i]), (int)FlJobsResult_NotFinished);
    }

    for (int i = 0; i < 4; i++) {
        atomic_store(&gates[i].release, true);
    }

    for (int i = 0; i < 4; i++) {
        fl_jobs_wait(handles[i]);
        ASSERT_EQ((int)fl_jobs_is_finished(handles[i]), (int)FlJobsResult_Finished);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(jobs_4t, basic_dependency) {
    (void)utest_fixture;

    DependencyTestData test_data;
    atomic_store(&test_data.next_order, 0);
    for (int i = 0; i < 10; i++) {
        atomic_store(&test_data.execution_order[i], (u32)-1);
    }

    FlJobHandle handle_a = fl_jobs_add_job(ordered_job_a, &test_data);
    FlJobHandle handle_b = fl_jobs_add_job_with_dependency(ordered_job_b, &test_data, handle_a);

    fl_jobs_wait(handle_b);

    int order_a = atomic_load(&test_data.execution_order[0]);
    int order_b = atomic_load(&test_data.execution_order[1]);

    ASSERT_TRUE(order_a < order_b);
    ASSERT_EQ(order_a, 0);
    ASSERT_EQ(order_b, 1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(jobs_4t, dependency_on_finished_job) {
    (void)utest_fixture;

    DependencyTestData test_data;
    atomic_store(&test_data.next_order, 0);
    for (int i = 0; i < 10; i++) {
        atomic_store(&test_data.execution_order[i], (u32)-1);
    }

    FlJobHandle handle_a = fl_jobs_add_job(ordered_job_a, &test_data);
    fl_jobs_wait(handle_a);

    FlJobHandle handle_b = fl_jobs_add_job_with_dependency(ordered_job_b, &test_data, handle_a);
    fl_jobs_wait(handle_b);

    int order_a = atomic_load(&test_data.execution_order[0]);
    int order_b = atomic_load(&test_data.execution_order[1]);

    ASSERT_EQ(order_a, 0);
    ASSERT_EQ(order_b, 1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(jobs_4t, dependency_race_condition) {
    (void)utest_fixture;

    DependencyTestData test_data;
    atomic_store(&test_data.next_order, 0);
    for (int i = 0; i < 10; i++) {
        atomic_store(&test_data.execution_order[i], (u32)-1);
    }

    FlJobHandle handle_a = fl_jobs_add_job(slow_job, &test_data);

    FlJobHandle handle_b = fl_jobs_add_job_with_dependency(ordered_job_b, &test_data, handle_a);

    fl_jobs_wait(handle_b);

    int order_a = atomic_load(&test_data.execution_order[0]);
    int order_b = atomic_load(&test_data.execution_order[1]);

    ASSERT_TRUE(order_a < order_b);
    ASSERT_EQ(order_a, 0);
    ASSERT_EQ(order_b, 1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(jobs_4t, multiple_dependents_on_one_job) {
    (void)utest_fixture;

    DependencyTestData test_data;
    atomic_store(&test_data.next_order, 0);
    for (int i = 0; i < 10; i++) {
        atomic_store(&test_data.execution_order[i], (u32)-1);
    }

    FlJobHandle handle_a = fl_jobs_add_job(ordered_job_a, &test_data);
    FlJobHandle handle_b = fl_jobs_add_job_with_dependency(ordered_job_b, &test_data, handle_a);
    FlJobHandle handle_c = fl_jobs_add_job_with_dependency(ordered_job_c, &test_data, handle_a);

    fl_jobs_wait(handle_b);
    fl_jobs_wait(handle_c);

    int order_a = atomic_load(&test_data.execution_order[0]);
    int order_b = atomic_load(&test_data.execution_order[1]);
    int order_c = atomic_load(&test_data.execution_order[2]);

    ASSERT_TRUE(order_a < order_b);
    ASSERT_TRUE(order_a < order_c);
    ASSERT_EQ(order_a, 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(jobs_4t, invalid_dependency_handle) {
    (void)utest_fixture;

    DependencyTestData test_data;
    atomic_store(&test_data.next_order, 0);
    atomic_store(&test_data.execution_order[0], (u32)-1);

    FlJobHandle invalid_handle = 0xDEADBEEF;
    FlJobHandle handle_a = fl_jobs_add_job_with_dependency(ordered_job_a, &test_data, invalid_handle);

    fl_jobs_wait(handle_a);

    int order_a = atomic_load(&test_data.execution_order[0]);
    ASSERT_EQ(order_a, 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(jobs_4t, dependency_with_zero_handle) {
    (void)utest_fixture;

    DependencyTestData test_data;
    atomic_store(&test_data.next_order, 0);
    atomic_store(&test_data.execution_order[0], (u32)-1);

    FlJobHandle handle_a = fl_jobs_add_job_with_dependency(ordered_job_a, &test_data, 0);

    fl_jobs_wait(handle_a);

    int order_a = atomic_load(&test_data.execution_order[0]);
    ASSERT_EQ(order_a, 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(jobs_4t, priority_basic) {
    (void)utest_fixture;

    PriorityOrderData test_data = { 0 };

    FlJobHandle low_handle = fl_jobs_add_job_with_priority(priority_low_job, &test_data, JobPriority_Low);
    FlJobHandle normal_handle = fl_jobs_add_job_with_priority(priority_normal_job, &test_data, JobPriority_Normal);
    FlJobHandle high_handle = fl_jobs_add_job_with_priority(priority_high_job, &test_data, JobPriority_High);

    fl_jobs_wait(low_handle);
    fl_jobs_wait(normal_handle);
    fl_jobs_wait(high_handle);

    ASSERT_EQ(atomic_load(&test_data.high_count), 1u);
    ASSERT_EQ(atomic_load(&test_data.normal_count), 1u);
    ASSERT_EQ(atomic_load(&test_data.low_count), 1u);
    ASSERT_EQ(atomic_load(&test_data.total_completed), 3u);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(jobs_4t, priority_default_is_normal) {
    (void)utest_fixture;

    PriorityOrderData test_data = { 0 };

    FlJobHandle handle = fl_jobs_add_job(priority_order_job, &test_data);

    FlJobPriority priority = fl_jobs_get_priority(handle);
    ASSERT_EQ((int)priority, (int)JobPriority_Normal);

    fl_jobs_wait(handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(jobs_4t, priority_with_dependency) {
    (void)utest_fixture;

    DependencyTestData test_data;
    atomic_store(&test_data.next_order, 0);
    for (int i = 0; i < 10; i++) {
        atomic_store(&test_data.execution_order[i], (u32)-1);
    }

    FlJobHandle handle_a = fl_jobs_add_job_with_priority(ordered_job_a, &test_data, JobPriority_High);
    FlJobHandle handle_b
        = fl_jobs_add_job_with_priority_and_dependency(ordered_job_b, &test_data, JobPriority_Normal, handle_a);

    fl_jobs_wait(handle_b);

    int order_a = atomic_load(&test_data.execution_order[0]);
    int order_b = atomic_load(&test_data.execution_order[1]);

    ASSERT_TRUE(order_a < order_b);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(jobs_4t, priority_stress_reprioritize) {
    (void)utest_fixture;

    const int num_jobs = 200;
    FlJobHandle handles[200];

    ReprioritizeTestData test_data = { .completed = 0, .handles = handles, .num_handles = num_jobs };

    for (int i = 0; i < num_jobs; i++) {
        handles[i] = fl_jobs_add_job_with_priority(reprioritize_job, &test_data, JobPriority_Low);
    }

    for (int i = 0; i < num_jobs / 2; i++) {
        fl_jobs_set_priority(handles[i], JobPriority_High);
    }

    for (int i = num_jobs / 2; i < num_jobs * 3 / 4; i++) {
        fl_jobs_set_priority(handles[i], JobPriority_Normal);
    }

    for (int i = 0; i < num_jobs; i++) {
        fl_jobs_wait(handles[i]);
    }

    ASSERT_EQ(atomic_load(&test_data.completed), (u32)num_jobs);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(jobs_4t, priority_stress_rapid_changes) {
    (void)utest_fixture;

    // Create blocker jobs to keep workers busy
    DelayedJob blocker_data[4];
    FlJobHandle blockers[4];
    for (int i = 0; i < 4; i++) {
        blocker_data[i].delay_ms = 50;
        blocker_data[i].job_id = i;
        blockers[i] = fl_jobs_add_job(delayed_job, &blocker_data[i]);
    }

    PriorityOrderData test_data = { 0 };

    FlJobHandle job = fl_jobs_add_job_with_priority(priority_low_job, &test_data, JobPriority_Low);

    // A worker may claim the job mid-loop if blockers finish or the main thread is
    // preempted, so we cannot assert a specific final priority value. The important
    // invariants are: the job executes exactly once, all jobs complete, and the system
    // remains in a consistent state.
    for (int i = 0; i < 10; i++) {
        fl_jobs_set_priority(job, JobPriority_High);
        fl_jobs_set_priority(job, JobPriority_Low);
        fl_jobs_set_priority(job, JobPriority_Normal);
        fl_jobs_set_priority(job, JobPriority_High);
    }

    for (int i = 0; i < 4; i++) {
        fl_jobs_wait(blockers[i]);
    }
    fl_jobs_wait(job);

    ASSERT_EQ(atomic_load(&test_data.low_count), 1u);
    ASSERT_EQ(atomic_load(&test_data.total_completed), 1u);

    for (int i = 0; i < 4; i++) {
        ASSERT_EQ(fl_jobs_is_finished(blockers[i]), FlJobsResult_Finished);
    }

    ASSERT_EQ(fl_jobs_is_finished(job), FlJobsResult_Finished);

    ASSERT_FALSE(fl_jobs_set_priority(job, JobPriority_Low));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(jobs_4t, priority_ordering_validation) {
    (void)utest_fixture;

    const int high_jobs = 50;
    const int low_jobs = 50;

    PriorityOrderValidationData test_data = { 0 };

    FlJobHandle high_handles[50];
    FlJobHandle low_handles[50];

    // Occupy every worker first, so all 100 jobs below are queued before any of them can be claimed.
    DelayedJob blocker_data[4];
    FlJobHandle blockers[4];
    for (int i = 0; i < 4; i++) {
        blocker_data[i].delay_ms = 50;
        blocker_data[i].job_id = i;
        blockers[i] = fl_jobs_add_job(delayed_job, &blocker_data[i]);
    }

    for (int i = 0; i < low_jobs; i++) {
        low_handles[i] = fl_jobs_add_job_with_priority(priority_validation_low_job, &test_data, JobPriority_Low);
    }

    for (int i = 0; i < high_jobs; i++) {
        high_handles[i] = fl_jobs_add_job_with_priority(priority_validation_high_job, &test_data, JobPriority_High);
    }

    for (int i = 0; i < low_jobs; i++) {
        fl_jobs_wait(low_handles[i]);
    }
    for (int i = 0; i < high_jobs; i++) {
        fl_jobs_wait(high_handles[i]);
    }
    for (int i = 0; i < 4; i++) {
        fl_jobs_wait(blockers[i]);
    }

    ASSERT_EQ(atomic_load(&test_data.total_high), (u32)high_jobs);
    ASSERT_EQ(atomic_load(&test_data.total_low), (u32)low_jobs);

    u32 total_high_before = atomic_load(&test_data.high_completions_before_low_start);
    u32 low_count = atomic_load(&test_data.low_started);
    float avg_high_before_low = (float)total_high_before / (float)low_count;

    // A queue that honours priority drains all 50 high jobs before the first low one starts, so every low job
    // observes 50 completed highs; one that ignores or inverts priority lands near 0. The bound is half the
    // ideal, leaving room for the few lows a worker may claim as the blockers stagger their release.
    ASSERT_GT(avg_high_before_low, 25.0f);
}

// =====================================================================================================
// 8-THREAD TESTS
// =====================================================================================================

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(jobs_8t, stress_test_dependencies) {
    (void)utest_fixture;

    const int num_chains = 10;
    const int chain_length = 5;
    FlJobHandle handles[10][5];
    TestData test_data[10];

    for (int i = 0; i < num_chains; i++) {
        atomic_store(&test_data[i].counter, 0);
        test_data[i].expected_increments = chain_length;
    }

    for (int chain = 0; chain < num_chains; chain++) {
        handles[chain][0] = fl_jobs_add_job(increment_counter, &test_data[chain]);

        for (int job = 1; job < chain_length; job++) {
            handles[chain][job]
                = fl_jobs_add_job_with_dependency(increment_counter, &test_data[chain], handles[chain][job - 1]);
        }
    }

    for (int chain = 0; chain < num_chains; chain++) {
        fl_jobs_wait(handles[chain][chain_length - 1]);
    }

    for (int chain = 0; chain < num_chains; chain++) {
        ASSERT_EQ((int)atomic_load(&test_data[chain].counter), chain_length);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(jobs_8t, priority_stress_mixed_submission) {
    (void)utest_fixture;

    const int jobs_per_priority = 100;
    const int total_jobs = jobs_per_priority * 3;

    PriorityOrderData test_data = { 0 };

    FlJobHandle handles[300];

    for (int i = 0; i < jobs_per_priority; i++) {
        handles[i * 3 + 0] = fl_jobs_add_job_with_priority(priority_low_job, &test_data, JobPriority_Low);
        handles[i * 3 + 1] = fl_jobs_add_job_with_priority(priority_normal_job, &test_data, JobPriority_Normal);
        handles[i * 3 + 2] = fl_jobs_add_job_with_priority(priority_high_job, &test_data, JobPriority_High);
    }

    for (int i = 0; i < total_jobs; i++) {
        fl_jobs_wait(handles[i]);
    }

    ASSERT_EQ(atomic_load(&test_data.high_count), (u32)jobs_per_priority);
    ASSERT_EQ(atomic_load(&test_data.normal_count), (u32)jobs_per_priority);
    ASSERT_EQ(atomic_load(&test_data.low_count), (u32)jobs_per_priority);
    ASSERT_EQ(atomic_load(&test_data.total_completed), (u32)total_jobs);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(jobs_8t, priority_stress_with_dependencies) {
    (void)utest_fixture;

    const int num_chains = 20;
    const int chain_length = 5;
    FlJobHandle handles[20][5];
    TestData test_data[20];

    for (int i = 0; i < num_chains; i++) {
        atomic_store(&test_data[i].counter, 0);
        test_data[i].expected_increments = chain_length;
    }

    for (int chain = 0; chain < num_chains; chain++) {
        JobPriority priority;
        if (chain < num_chains / 3) {
            priority = JobPriority_High;
        } else if (chain < num_chains * 2 / 3) {
            priority = JobPriority_Normal;
        } else {
            priority = JobPriority_Low;
        }

        handles[chain][0] = fl_jobs_add_job_with_priority(increment_counter, &test_data[chain], priority);

        for (int job = 1; job < chain_length; job++) {
            handles[chain][job] = fl_jobs_add_job_with_priority_and_dependency(increment_counter, &test_data[chain],
                                                                               priority, handles[chain][job - 1]);
        }
    }

    for (int chain = 0; chain < num_chains; chain++) {
        fl_jobs_wait(handles[chain][chain_length - 1]);
    }

    for (int chain = 0; chain < num_chains; chain++) {
        ASSERT_EQ((int)atomic_load(&test_data[chain].counter), chain_length);
    }
}
