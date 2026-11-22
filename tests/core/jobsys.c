// job-system wakeup, dependency and queue saturation tests

#include "unit_tests_common.h"

#define JOBSYS_WAIT_5S 5000000000ull
#define JOBSYS_WAIT_10S 10000000000ull
#define JOBSYS_WAIT_15S 15000000000ull

typedef struct JobsysWaitTarget {
    const _Atomic u64* counter;
    u64 target;
} JobsysWaitTarget;

// Waits until every counter reached its target or timeout_ns passed, sleeping poll_us between checks when it is
// non-zero. True when all of them reached it.
static bool jobsys_wait_reached(const JobsysWaitTarget* targets, int count, u64 timeout_ns, u32 poll_us) {
    const u64 deadline_ns = fl_os_monotonic_ns() + timeout_ns;
    for (;;) {
        bool all_reached = true;
        for (int i = 0; i < count; i++) {
            all_reached = all_reached && atomic_load(targets[i].counter) >= targets[i].target;
        }
        if (all_reached) {
            return true;
        }
        if (fl_os_monotonic_ns() >= deadline_ns) {
            return false;
        }
        if (poll_us > 0) {
            fl_os_sleep_us(poll_us);
        }
    }
}

static bool jobsys_wait_counter(const _Atomic u64* counter, u64 target, u64 timeout_ns, u32 poll_us) {
    const JobsysWaitTarget single = { .counter = counter, .target = target };
    return jobsys_wait_reached(&single, 1, timeout_ns, poll_us);
}

// A job enqueued just as the single worker goes idle is still picked up. The tight add-then-wait loop keeps the
// worker moving in and out of the idle state, so many adds land in the window where the producer decides whether
// to signal.

static _Atomic u64 s_jobsys_stress_counter;

static void jobsys_stress_job(void* user_data, FlJobsWorkerInfo worker_info) {
    (void)user_data;
    (void)worker_info;
    atomic_fetch_add(&s_jobsys_stress_counter, 1);
}

UTEST(jobsys, no_lost_wakeup_under_rapid_idle_transitions) {
    const u64 iterations = 20000;
    atomic_store(&s_jobsys_stress_counter, 0);

    for (u64 i = 0; i < iterations; i++) {
        FlJobHandle handle = fl_jobs_add_job(jobsys_stress_job, nullptr);
        ASSERT_TRUE(handle != 0);

        // Bounded spin-wait for this job to run. If the wakeup is lost the worker never picks it up and the
        // counter never reaches i+1, so the 5s deadline turns the hang into a test failure.
        ASSERT_TRUE_MSG(jobsys_wait_counter(&s_jobsys_stress_counter, i + 1, JOBSYS_WAIT_5S, 0),
                        "job was stranded — worker slept with a job queued (lost wakeup)");
    }

    ASSERT_EQ_MSG(atomic_load(&s_jobsys_stress_counter), iterations, "not every job ran");
}

// A dependent registered on a dependency that finishes at almost the same instant is always scheduled. The
// dependency spins briefly so that, over many iterations, its completion often overlaps the dependent's
// registration, which is the window where promotion races with slot recycling.

static _Atomic u64 s_jobsys_dep_ran;

static void jobsys_dep_spin_job(void* user_data, FlJobsWorkerInfo worker_info) {
    (void)worker_info;
    // A short busy spin, so the dependency is often still running when the dependent is registered.
    volatile u64 sink = 0;
    for (u64 i = 0; i < 200; i++) {
        sink += i;
    }
    atomic_fetch_add((_Atomic u64*)user_data, 1);
}

static void jobsys_dependent_job(void* user_data, FlJobsWorkerInfo worker_info) {
    (void)user_data;
    (void)worker_info;
    atomic_fetch_add(&s_jobsys_dep_ran, 1);
}

UTEST(jobsys, dependent_promoted_when_dependency_completes_concurrently) {
    const u64 iterations = 20000;
    _Atomic u64 dep_counter;
    atomic_store(&dep_counter, 0);
    atomic_store(&s_jobsys_dep_ran, 0);

    for (u64 i = 0; i < iterations; i++) {
        FlJobHandle dep = fl_jobs_add_job(jobsys_dep_spin_job, &dep_counter);
        ASSERT_TRUE(dep != 0);

        FlJobHandle dependent = fl_jobs_add_job_with_dependency(jobsys_dependent_job, nullptr, dep);
        ASSERT_TRUE(dependent != 0);

        // Bounded wait for the dependent. If it was lost between promotion and completion it never runs, and
        // the 5s deadline turns the hang into a test failure.
        ASSERT_TRUE_MSG(jobsys_wait_counter(&s_jobsys_dep_ran, i + 1, JOBSYS_WAIT_5S, 0),
                        "dependent was orphaned — promoted after the dependency was recycled");
    }

    ASSERT_EQ_MSG(atomic_load(&s_jobsys_dep_ran), iterations, "not every dependent ran");
}

// Queue saturation. When all SLOTS entries of a work queue are occupied, a producer that is not a worker blocks
// until a slot frees, and a worker producer never blocks: it claims the job and runs it inline. Both tests rely on
// the suite running with exactly one worker thread (fl_init(1) in main), so that a held worker means nothing
// drains the queue.

static _Atomic u64 s_jobsys_sat_gate_started;

static _Atomic u64 s_jobsys_sat_gate_release;

// Occupies the single worker: signals that it has started, then holds until released.
static void jobsys_sat_gate_job(void* user_data, FlJobsWorkerInfo worker_info) {
    (void)user_data;
    (void)worker_info;
    atomic_store(&s_jobsys_sat_gate_started, 1);
    while (atomic_load(&s_jobsys_sat_gate_release) == 0) {
        fl_os_sleep_us(10);
    }
}

static void jobsys_sat_count_job(void* user_data, FlJobsWorkerInfo worker_info) {
    (void)worker_info;
    atomic_fetch_add((_Atomic u64*)user_data, 1);
}

// Adds the gate job that holds the worker. The caller waits on s_jobsys_sat_gate_started.
static FlJobHandle jobsys_sat_hold_worker(void) {
    atomic_store(&s_jobsys_sat_gate_started, 0);
    atomic_store(&s_jobsys_sat_gate_release, 0);
    return fl_jobs_add_job(jobsys_sat_gate_job, nullptr);
}

// Adds SLOTS counting jobs, which fill the queue once the worker has dequeued the gate. False when an add failed.
static bool jobsys_sat_fill_queue(_Atomic u64* counter) {
    for (u64 i = 0; i < SLOTS; i++) {
        if (fl_jobs_add_job(jobsys_sat_count_job, counter) == 0) {
            return false;
        }
    }
    return true;
}

// Releases the gate after a short delay from a thread that is neither the main thread nor the worker, so the main
// thread can be blocked in a saturated add when the release happens.
static void* jobsys_sat_delayed_release_thread(void* arg) {
    (void)arg;
    sleep_ms(50);
    atomic_store(&s_jobsys_sat_gate_release, 1);
    return nullptr;
}

// A producer that is not a worker blocks when it adds to a full queue and completes once the worker drains a
// slot.
UTEST(jobsys, producer_backpressure_when_queue_saturated) {
    static _Atomic u64 ran;
    atomic_store(&ran, 0);

    // Occupy the worker so nothing drains the queue while it is filled.
    FlJobHandle gate = jobsys_sat_hold_worker();
    ASSERT_TRUE(gate != 0);
    ASSERT_TRUE_MSG(jobsys_wait_counter(&s_jobsys_sat_gate_started, 1, JOBSYS_WAIT_5S, 0), "gate job never started");

    // The worker dequeued the gate, so exactly SLOTS entries fit without saturating.
    ASSERT_TRUE(jobsys_sat_fill_queue(&ran));

    // The queue is full and the worker is still held, so the add below blocks. It completes only after the
    // helper thread's delayed release lets the worker start draining.
    Thread releaser;
    ASSERT_TRUE(os_thread_create(&releaser, jobsys_sat_delayed_release_thread, nullptr));
    ASSERT_TRUE(fl_jobs_add_job(jobsys_sat_count_job, &ran) != 0);
    os_thread_join(&releaser);

    ASSERT_TRUE_MSG(jobsys_wait_counter(&ran, SLOTS + 1, JOBSYS_WAIT_10S, 100), "not every job ran after saturation");
    ASSERT_EQ_MSG(atomic_load(&ran), (u64)SLOTS + 1, "not every job ran after saturation");
}

// A worker promoting dependents into a full queue runs them inline. With one worker there is no other consumer,
// so blocking would deadlock. The gate holds the worker while the queue is filled and dependents are registered,
// and releasing it triggers promotion while the queue is still full.
UTEST(jobsys, dependents_execute_inline_when_queue_saturated) {
    static _Atomic u64 filler_ran;
    static _Atomic u64 dep_ran;
    atomic_store(&filler_ran, 0);
    atomic_store(&dep_ran, 0);

    FlJobHandle gate = jobsys_sat_hold_worker();
    ASSERT_TRUE(gate != 0);
    ASSERT_TRUE_MSG(jobsys_wait_counter(&s_jobsys_sat_gate_started, 1, JOBSYS_WAIT_5S, 0), "gate job never started");

    // Fill the queue to capacity while the worker is held on the gate.
    ASSERT_TRUE(jobsys_sat_fill_queue(&filler_ran));

    // Dependents go on the gate's waiting list and reach the queue only during promotion, which runs on the
    // worker right after the gate's function returns, while the queue is still full.
    const u64 num_dependents = 8;
    for (u64 i = 0; i < num_dependents; i++) {
        ASSERT_TRUE(fl_jobs_add_job_with_dependency(jobsys_sat_count_job, &dep_ran, gate) != 0);
    }

    atomic_store(&s_jobsys_sat_gate_release, 1);

    const JobsysWaitTarget drained[] = { { &dep_ran, num_dependents }, { &filler_ran, SLOTS } };
    ASSERT_TRUE_MSG(jobsys_wait_reached(drained, 2, JOBSYS_WAIT_10S, 100),
                    "saturated promotion deadlocked — dependents or fillers never ran");
}

// A job that a worker runs inline off a saturated queue runs nested, on the stack of a job that is still using
// the same scratch arena. It gives back only what it allocated. Rewinding to the arena floor would poison the
// outer job's live allocations and let its next one alias them.

#define NESTED_SCRATCH_BYTES 256

typedef struct JobsysNestedCtx {
    _Atomic u64 outer_started;
    _Atomic u64 queue_saturated;
    _Atomic u64 child_ran;
    _Atomic u64 pattern_survived;
    _Atomic u64 second_alloc_aliased;
    _Atomic u64 outer_done;
    FlJobHandle settled_dependency;
} JobsysNestedCtx;

static void jobsys_nested_noop_job(void* user_data, FlJobsWorkerInfo worker_info) {
    (void)user_data;
    (void)worker_info;
}

static void jobsys_nested_flag_job(void* user_data, FlJobsWorkerInfo worker_info) {
    (void)worker_info;
    atomic_store((_Atomic u64*)user_data, 1);
}

static void jobsys_nested_child_job(void* user_data, FlJobsWorkerInfo worker_info) {
    (void)worker_info;
    atomic_fetch_add(&((JobsysNestedCtx*)user_data)->child_ran, 1);
}

static void jobsys_nested_fill_pattern(u8* pattern) {
    for (u32 i = 0; i < NESTED_SCRATCH_BYTES; i++) {
        pattern[i] = (u8)(i + 1);
    }
}

static bool jobsys_nested_pattern_is_intact(const u8* pattern) {
    for (u32 i = 0; i < NESTED_SCRATCH_BYTES; i++) {
        if (pattern[i] != (u8)(i + 1)) {
            return false;
        }
    }
    return true;
}

static void jobsys_nested_outer_job(void* user_data, FlJobsWorkerInfo worker_info) {
    JobsysNestedCtx* ctx = (JobsysNestedCtx*)user_data;

    u8* pattern = arena_alloc_array(worker_info.scratch, u8, NESTED_SCRATCH_BYTES);
    jobsys_nested_fill_pattern(pattern);

    atomic_store(&ctx->outer_started, 1);
    while (atomic_load(&ctx->queue_saturated) == 0) {
        fl_os_sleep_us(200);
    }

    // A dependency that finished but was never polled gets past the early-outs and registers, so the add ends in
    // a schedule. With every queue slot taken, that schedule runs the child inline, nested right here.
    fl_jobs_add_job_with_dependency(jobsys_nested_child_job, ctx, ctx->settled_dependency);

    // Allocated after the nested run returned, so it aliases pattern only if the arena was rewound to its floor.
    // The fill then destroys pattern, which the check below reads.
    u8* second = arena_alloc_array(worker_info.scratch, u8, NESTED_SCRATCH_BYTES);
    memset(second, 0xAB, NESTED_SCRATCH_BYTES);
    atomic_store(&ctx->second_alloc_aliased, second == pattern ? 1u : 0u);

    atomic_store(&ctx->pattern_survived, jobsys_nested_pattern_is_intact(pattern) ? 1u : 0u);
    atomic_store(&ctx->outer_done, 1);
}

static void jobsys_nested_ctx_reset(JobsysNestedCtx* ctx) {
    atomic_store(&ctx->outer_started, 0);
    atomic_store(&ctx->queue_saturated, 0);
    atomic_store(&ctx->child_ran, 0);
    atomic_store(&ctx->pattern_survived, 0);
    atomic_store(&ctx->second_alloc_aliased, 1);
    atomic_store(&ctx->outer_done, 0);
}

UTEST(jobsys, nested_inline_job_leaves_the_outer_jobs_scratch_alone) {
    static JobsysNestedCtx ctx;
    jobsys_nested_ctx_reset(&ctx);

    // Completion is observed through a second job, in order on the one worker, because polling it with
    // fl_jobs_is_finished would recycle the slot the add below needs to still carry its generation.
    static _Atomic u64 ordering_flag;
    atomic_store(&ordering_flag, 0);
    ctx.settled_dependency = fl_jobs_add_job(jobsys_nested_noop_job, nullptr);
    ASSERT_TRUE(ctx.settled_dependency != 0);
    ASSERT_TRUE(fl_jobs_add_job(jobsys_nested_flag_job, &ordering_flag) != 0);
    ASSERT_TRUE_MSG(jobsys_wait_counter(&ordering_flag, 1, JOBSYS_WAIT_5S, 0), "the dependency never completed");

    ASSERT_TRUE(fl_jobs_add_job(jobsys_nested_outer_job, &ctx) != 0);
    ASSERT_TRUE_MSG(jobsys_wait_counter(&ctx.outer_started, 1, JOBSYS_WAIT_5S, 0), "the outer job never started");

    // No assertion from here until the outer job is released: it holds the only worker, and an ASSERT returns
    // from the case and leaves it spinning. The worker dequeued the outer job, so exactly SLOTS entries fit.
    // Filling past that would block this thread with nothing left to drain the queue.
    static _Atomic u64 filler_ran;
    atomic_store(&filler_ran, 0);
    const bool queue_filled = jobsys_sat_fill_queue(&filler_ran);
    atomic_store(&ctx.queue_saturated, 1);

    const bool outer_finished = jobsys_wait_counter(&ctx.outer_done, 1, JOBSYS_WAIT_10S, 200);

    ASSERT_TRUE_MSG(queue_filled, "the queue could not be filled to capacity");
    ASSERT_TRUE_MSG(outer_finished, "the outer job never finished its probe");
    ASSERT_EQ_MSG(atomic_load(&ctx.child_ran), 1u, "the child must run exactly once, inline on the worker");
    const u64 second_alloc_aliased = atomic_load(&ctx.second_alloc_aliased);
    ASSERT_EQ_MSG(second_alloc_aliased, 0u,
                  "a nested job rewound the scratch arena to its floor, so the next allocation aliased the "
                  "outer job's live one");
    ASSERT_EQ_MSG(atomic_load(&ctx.pattern_survived), 1u,
                  "a nested job freed the outer job's scratch allocation out from under it");

    // Let the fillers drain so the next case starts against an empty queue.
    ASSERT_TRUE_MSG(jobsys_wait_counter(&filler_ran, SLOTS, JOBSYS_WAIT_15S, 100), "the saturating fillers never drained");
}
