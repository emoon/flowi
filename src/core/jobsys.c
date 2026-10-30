#include "core.h"
#include "assert.h"
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "jobsys.h"
#include "mcmpq.h"

#include "arena.h"
#include "log.h"
#include "os/os.h"
#include "pool_allocator.h"

#define JOB_HANDLE_INDEX_MASK 0xFFFFFFFF00000000
#define JOB_HANDLE_GEN_MASK 0x00000000FFFFFFFF
#define JOB_HANDLE_INDEX_SHIFT 32

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

enum JobStatus {
    JOB_STATUS_NOT_STARTED = 0,
    JOB_STATUS_CLAIMED = 1,
    JOB_STATUS_COMPLETED = 2,
    JOB_STATUS_FREE = 3, // Job is completed and has been added to free list
    JOB_STATUS_WAITING_FOR_DEPENDENCY = 4,
    // Function has run and dependents are being drained. Treated as "done" by an adder's self-promotion
    // check (so a late dependent still gets scheduled) but NOT as recyclable by fl_jobs_is_finished, so
    // the slot stays pinned until promote_dependent_jobs finishes and JOB_STATUS_COMPLETED is published.
    JOB_STATUS_PROMOTING = 5,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct JobInfo {
    CACHE_ALIGNED _Atomic(FlJobsFunc) function;
    CACHE_ALIGNED _Atomic(void*) user_data;
    CACHE_ALIGNED _Atomic u32 status;
    CACHE_ALIGNED _Atomic u32 generation;
    CACHE_ALIGNED _Atomic(struct JobInfo*) first_dependent; // Head of intrusive linked list of dependent jobs
    CACHE_ALIGNED _Atomic u32 priority;                     // Current priority level (JobPriority enum)
    struct JobInfo* next_dependent;
    struct JobInfo* next; // Free list pointer
} JobInfo;

struct WorkerData;

static void promote_dependent_jobs(JobInfo* completed_job);
static void execute_job(JobInfo* work_job, FlJobsWorkerInfo* worker_info, bool nested);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct {
    u8 data[1024];
} JobContext;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct Jobs {
    CACHE_ALIGNED _Atomic u32 shutdown;
    FlArena* arena;
    queue_t work_queues[JOB_PRIORITY_COUNT]; // Lock-free queues per priority level (Low, Normal, High)
    JobInfo* first_free_entity;              // Free job objects, guarded by free_list_mutex
    Mutex free_list_mutex; // Guards first_free_entity and job arena growth. Contended from any thread: jobs
                           // are added and polled off the main thread (e.g. an I/O subsystem issuing work),
                           // and a worker takes it too when it parks a dependent behind a live dependency
    Thread* threads;
    bool* threads_created; // Per-slot creation success; failed slots must never be joined
    JobInfo* start_jobs;
    struct WorkerData* thread_info;
    _Atomic u64 total_jobs_allocated; // Written by main thread, read by workers
    int num_threads;
    FlArena* context_arena;
    pool(JobContext) context_pool;
    Mutex context_mutex;
    void* context_pool_start;
    void* context_pool_end;
    Mutex idle_mutex; // Protects idle state transitions
    CondVar work_available;
    _Atomic u32 idle_workers;
} Jobs;

_Atomic(Jobs*) g_global_job_system = nullptr;

thread_local static bool s_is_main_thread = true;

// Thread-local pointer to current worker info (nullptr on main thread, valid on worker threads)
thread_local static FlJobsWorkerInfo* s_current_worker_info = nullptr;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void jobs_context_free(void* context) {
    if (!context)
        return;

    Jobs* self = g_global_job_system;
    FL_VALIDATE(self != nullptr);

    if (context >= self->context_pool_start && context < self->context_pool_end) {
        mutex_lock(&self->context_mutex);
        pool_free(&self->context_pool, (JobContext*)context);
        mutex_unlock(&self->context_mutex);
    }
    // If not from our pool, silently ignore (not an error)
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// An out-of-range priority (including FlJobPriority_Invalid) runs as High. Applied before the priority is stored so
// the stored value always names the queue the job sits in.
static JobPriority clamp_priority(JobPriority priority) {
    return (u32)priority >= JOB_PRIORITY_COUNT ? JobPriority_High : priority;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void schedule_job_with_priority(Jobs* self, JobInfo* job, JobPriority priority) {
    u32 queue = (u32)priority;

    if (!try_enqueue(&self->work_queues[queue], &job)) {
        // The queue is saturated and a worker must not block on a slot: with every worker stuck
        // promoting dependents into full queues, nothing is left to drain them. Claim the job with the
        // same status CAS a dequeuing worker would use and run it inline - the queue never saw it, so
        // it cannot run twice. This recurses one stack frame per chained dependent while saturation
        // persists.
        if (s_current_worker_info != nullptr) {
            u32 expected = JOB_STATUS_NOT_STARTED;
            if (atomic_compare_exchange_strong(&job->status, &expected, JOB_STATUS_CLAIMED)) {
                execute_job(job, s_current_worker_info, true);
            }
            // CAS failure means another thread already owns the job - nothing left to do here.
            return;
        }
        // Non-worker producer (main thread, a subsystem's own threads): apply backpressure until a slot frees.
        // Workers never block on a full queue, so the queue keeps draining while queued jobs terminate.
        while (!try_enqueue(&self->work_queues[queue], &job)) {
            sleep_us(50);
        }
    }

    // Reading idle_workers unlocked is safe against the going-idle path: wait_for_work increments it
    // before its final queue re-check, so a re-check that misses this enqueue is observed here and
    // signalled under idle_mutex below.
    if (atomic_load(&self->idle_workers) > 0) {
        mutex_lock(&self->idle_mutex);
        condvar_signal(&self->work_available);
        mutex_unlock(&self->idle_mutex);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void schedule_job(Jobs* self, JobInfo* job) {
    JobPriority priority = (JobPriority)atomic_load(&job->priority);
    schedule_job_with_priority(self, job, priority);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void promote_dependent_jobs(JobInfo* completed_job) {
    Jobs* self = g_global_job_system;
    FL_VALIDATE(self != nullptr);

    // Taking the whole list at once prevents new dependents from being added and gives us
    // exclusive access
    JobInfo* dependent = atomic_exchange(&completed_job->first_dependent, nullptr);

    while (dependent != nullptr) {
        JobInfo* next = dependent->next_dependent;

        // The CAS handles the race with self-promotion (see fl_jobs_add_job_with_dependency)
        u32 expected = JOB_STATUS_WAITING_FOR_DEPENDENCY;
        if (atomic_compare_exchange_strong(&dependent->status, &expected, JOB_STATUS_NOT_STARTED)) {
            schedule_job(self, dependent);
        }
        // If CAS failed, the job already self-promoted - skip it

        dependent = next;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Try to dequeue a job from priority queues (High -> Normal -> Low)
// Handles stale entries from reprioritization by checking job->priority matches queue

static bool try_dequeue_prioritized(Jobs* self, JobInfo** out_job) {
    for (int p = JOB_PRIORITY_COUNT - 1; p >= 0; p--) {
        JobInfo* job = nullptr;
        while (try_dequeue(&self->work_queues[p], &job)) {
            u32 current_priority = atomic_load(&job->priority);
            if (current_priority != (u32)p) {
                // Stale entry - the job will be picked up from its correct queue
                continue;
            }

            u32 expected = JOB_STATUS_NOT_STARTED;
            if (atomic_compare_exchange_strong(&job->status, &expected, JOB_STATUS_CLAIMED)) {
                *out_job = job;
                return true;
            }
            // Job was already claimed or completed (possible due to race), try next
        }
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void execute_job(JobInfo* work_job, FlJobsWorkerInfo* worker_info, bool nested) {
    FlJobsFunc func = atomic_load(&work_job->function);
    void* user_data = atomic_load(&work_job->user_data);
    FlTempArena scratch_scope = fl_arena_temp_begin(worker_info->scratch);
    func(user_data, *worker_info);

    jobs_context_free(user_data);
    // A nested run is inline on a worker whose outer job is still on the stack holding its own scratch
    // allocations, so it may only give back what it took. fl_arena_rewind would take the whole arena: it drops
    // to the rewind floor, poisons everything above it and releases the pages.
    if (nested) {
        fl_arena_temp_end(scratch_scope);
    } else {
        fl_arena_rewind(worker_info->scratch);
    }
    // Publish PROMOTING (release) before draining dependents: it is the release point for the job's
    // writes, and the signal a racing fl_jobs_add_job_with_dependency needs to self-promote a dependent
    // prepended after the drain below. PROMOTING is not recyclable - fl_jobs_is_finished reports it as
    // not-yet-finished - so the slot cannot be freed while promote_dependent_jobs still walks the list.
    atomic_store_explicit(&work_job->status, JOB_STATUS_PROMOTING, memory_order_release);
    // Promote any jobs that were waiting for this one to complete, while the slot is still pinned.
    promote_dependent_jobs(work_job);
    // Dependents are drained; only now is the job fully done and eligible for recycling.
    atomic_store_explicit(&work_job->status, JOB_STATUS_COMPLETED, memory_order_release);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void wait_for_work(Jobs* self, FlJobsWorkerInfo* worker_info) {
    mutex_lock(&self->idle_mutex);

    // Register as idle BEFORE re-checking the queues, all under idle_mutex. A producer enqueuing after
    // the re-check then observes this increment - the relevant atomics, including the seq_cst queue in
    // mcmpq.h, share one total order - and signals under idle_mutex instead of skipping. Counting a
    // worker idle while it is still re-checking is harmless: the re-check below claims any queued job.
    atomic_fetch_add(&self->idle_workers, 1);

    JobInfo* work_job = nullptr;
    if (try_dequeue_prioritized(self, &work_job)) {
        atomic_fetch_sub(&self->idle_workers, 1);
        mutex_unlock(&self->idle_mutex);
        execute_job(work_job, worker_info, false);
        return;
    }

    // Check shutdown flag while holding mutex to prevent race condition
    // between shutdown signal and going to sleep
    if (atomic_load(&self->shutdown)) {
        atomic_fetch_sub(&self->idle_workers, 1);
        mutex_unlock(&self->idle_mutex);
        return;
    }

    condvar_wait(&self->work_available, &self->idle_mutex);
    atomic_fetch_sub(&self->idle_workers, 1);

    mutex_unlock(&self->idle_mutex);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct WorkerData {
    Jobs* jobs;
    int index;
} WorkerData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* worker_thread(void* arg) {
    WorkerData* worker_info = arg;
    Jobs* self = worker_info->jobs;

    char thread_name[16];
    snprintf(thread_name, sizeof(thread_name), "job-worker-%d", worker_info->index);
    os_thread_set_name(thread_name);

    s_is_main_thread = false;

    arena_scratch_init();

    FlJobsWorkerInfo info = {
        .worker_index = worker_info->index,
        .scratch = arena_new(),
    };

    // Set thread-local worker info pointer for immediate execution detection
    s_current_worker_info = &info;

    while (true) {
        if (atomic_load(&self->shutdown)) {
            fl_arena_destroy(info.scratch);
            arena_scratch_destroy();
            return nullptr;
        }

        JobInfo* work_job = nullptr;
        if (try_dequeue_prioritized(self, &work_job)) {
            execute_job(work_job, &info, false);
        } else {
            wait_for_work(self, &info);
        }
    }

    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_jobs_create(FlArena* arena, const int num_threads) {
    // One job system per process, first caller wins. A dlopen'd plugin reaches the foundation through
    // its own copy of the Rust facade, whose "is the foundation up?" flag is a static in that module,
    // so it calls in here again on a process that already has a pool.
    if (atomic_load(&g_global_job_system) != nullptr) {
        return;
    }

    Jobs* self = arena_alloc_zero(arena, Jobs);
    self->arena = arena_new();

    self->first_free_entity = nullptr;
    self->total_jobs_allocated = 0;
    mutex_init(&self->free_list_mutex);

    mutex_init(&self->idle_mutex);
    condvar_init(&self->work_available);
    atomic_store(&self->idle_workers, 0);

    self->threads = arena_alloc_array_zero(self->arena, Thread, num_threads);
    self->threads_created = arena_alloc_array_zero(self->arena, bool, num_threads);
    self->num_threads = num_threads;
    self->thread_info = arena_alloc_array(self->arena, struct WorkerData, num_threads);

    self->context_arena = arena_new();
    mutex_init(&self->context_mutex);
    pool_new(&self->context_pool, self->context_arena);

    // Pointer range used by jobs_context_free to tell pool contexts from foreign pointers
    self->context_pool_start = self->context_arena->ptr;
    self->context_pool_end = (u8*)self->context_arena->ptr + self->context_arena->reserved_size;

    // Align up the arena so we know that we start at the correct offset
    fl_arena_align(self->arena, 64);

    self->start_jobs = (JobInfo*)((u8*)self->arena->ptr + self->arena->pos);

    // Set global system before starting threads to avoid race condition
    g_global_job_system = self;

    for_count(i, num_threads) {
        self->thread_info[i].jobs = self;
        self->thread_info[i].index = i;
        bool success = os_thread_create(&self->threads[i], worker_thread, &self->thread_info[i]);
        self->threads_created[i] = success;
        if (!success) {
            log_error("Failed to create worker thread %d", i);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlJobHandle fl_jobs_add_job_with_priority(FlJobsFunc func, void* user_data, JobPriority priority) {
    Jobs* self = g_global_job_system;
    priority = clamp_priority(priority);

    FL_VALIDATE_RET(self != nullptr, 0);
    FL_VALIDATE_RET(func != nullptr, 0);

    // fl_arena_temp_begin/end, not fl_arena_rewind: the caller may hold allocations made on info.scratch
    // before this job was scheduled.
    if (!fl_jobs_is_main_thread() && s_current_worker_info) {
        FlTempArena temp = fl_arena_temp_begin(s_current_worker_info->scratch);
        func(user_data, *s_current_worker_info);
        jobs_context_free(user_data);
        fl_arena_temp_end(temp);
        return JOB_HANDLE_IMMEDIATE_COMPLETE;
    }

    // Get a job from the free list or allocate. Guarded: jobs are added from any non-worker thread.
    mutex_lock(&self->free_list_mutex);
    JobInfo* job = self->first_free_entity;
    if (job != nullptr) {
        self->first_free_entity = self->first_free_entity->next;
        atomic_fetch_add(&job->generation, 1); // Increment generation to invalidate old handles
    } else {
        job = arena_alloc_zero(self->arena, JobInfo);
        atomic_fetch_add(&job->generation, 1);
        atomic_fetch_add(&self->total_jobs_allocated, 1);
    }
    mutex_unlock(&self->free_list_mutex);

    u64 job_index = (u64)(job - self->start_jobs);
    u64 generation = atomic_load(&job->generation);
    u64 handle = (job_index << JOB_HANDLE_INDEX_SHIFT) | generation;

    atomic_store(&job->function, func);
    atomic_store(&job->user_data, user_data);
    atomic_store(&job->first_dependent, nullptr);
    atomic_store(&job->priority, (u32)priority); // Store priority for stale entry detection
    job->next_dependent = nullptr;
    // Status must be the last write: NOT_STARTED makes the job claimable by any worker, so every
    // field a worker reads when executing must already be published (matches the dependency path).
    atomic_store(&job->status, JOB_STATUS_NOT_STARTED);

    schedule_job_with_priority(self, job, priority);

    return handle;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlJobHandle fl_jobs_add_job(FlJobsFunc func, void* user_data) {
    return fl_jobs_add_job_with_priority(func, user_data, JobPriority_Normal);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlJobHandle fl_jobs_add_job_with_priority_and_dependency(FlJobsFunc func, void* user_data, JobPriority priority,
                                                         FlJobHandle dependency) {
    Jobs* self = g_global_job_system;
    priority = clamp_priority(priority);

    FL_VALIDATE_RET(self != nullptr, 0);
    FL_VALIDATE_RET(func != nullptr, 0);

    if (dependency == 0) {
        return fl_jobs_add_job_with_priority(func, user_data, priority);
    }

    if (dependency == JOB_HANDLE_IMMEDIATE_COMPLETE) {
        return fl_jobs_add_job_with_priority(func, user_data, priority);
    }

    const u64 dep_index = (dependency & JOB_HANDLE_INDEX_MASK) >> JOB_HANDLE_INDEX_SHIFT;
    const u64 dep_generation = dependency & JOB_HANDLE_GEN_MASK;

    if (dep_index >= atomic_load(&self->total_jobs_allocated)) {
        return fl_jobs_add_job_with_priority(func, user_data, priority);
    }

    JobInfo* dep_job = &self->start_jobs[dep_index];

    // Not paired with the status read below: the dependency can finish and its slot be reused across the
    // free-list section, leaving that read and the prepend on the new occupant. Harmless - a slot is only
    // recycled after its job completed, so the dependency is done, and the re-check after the prepend
    // releases the job when the occupant reports COMPLETED/FREE/PROMOTING. Costs a wait, never an order.
    if (atomic_load(&dep_job->generation) != dep_generation) {
        // Dependency was recycled - treat as no dependency
        return fl_jobs_add_job_with_priority(func, user_data, priority);
    }

    // Get a job from the free list or allocate. Guarded: a worker reaches this too, so the free list is
    // contended by workers and producers alike.
    mutex_lock(&self->free_list_mutex);
    JobInfo* job = self->first_free_entity;
    if (job != nullptr) {
        self->first_free_entity = self->first_free_entity->next;
        atomic_fetch_add(&job->generation, 1); // Increment generation to invalidate old handles
    } else {
        job = arena_alloc_zero(self->arena, JobInfo);
        atomic_fetch_add(&job->generation, 1);
        atomic_fetch_add(&self->total_jobs_allocated, 1);
    }
    mutex_unlock(&self->free_list_mutex);

    u64 job_index = (u64)(job - self->start_jobs);
    u64 generation = atomic_load(&job->generation);
    u64 handle = (job_index << JOB_HANDLE_INDEX_SHIFT) | generation;

    atomic_store(&job->function, func);
    atomic_store(&job->user_data, user_data);
    atomic_store(&job->first_dependent, nullptr);
    atomic_store(&job->priority, (u32)priority); // Store priority for stale entry detection
    job->next_dependent = nullptr;

    // PROMOTING counts as complete: its function has run and its dependent list has been, or is being,
    // drained, so a new dependent must be scheduled directly rather than enqueued onto that list.
    u32 dep_status = atomic_load_explicit(&dep_job->status, memory_order_acquire);
    if (dep_status == JOB_STATUS_COMPLETED || dep_status == JOB_STATUS_FREE || dep_status == JOB_STATUS_PROMOTING) {
        atomic_store(&job->status, JOB_STATUS_NOT_STARTED);
        schedule_job_with_priority(self, job, priority);
        return handle;
    }

    atomic_store(&job->status, JOB_STATUS_WAITING_FOR_DEPENDENCY);

    // Add this job to the dependency's dependent list (lock-free prepend)
    JobInfo* expected_head = atomic_load(&dep_job->first_dependent);
    do {
        job->next_dependent = expected_head;
    } while (!atomic_compare_exchange_weak(&dep_job->first_dependent, &expected_head, job));

    // The dependency can complete between the check above and the list insert. PROMOTING is published
    // (release) before the drain, so if the drain already passed the just-added job this re-check
    // observes at least PROMOTING and self-promotes it here.
    dep_status = atomic_load_explicit(&dep_job->status, memory_order_acquire);
    if (dep_status == JOB_STATUS_COMPLETED || dep_status == JOB_STATUS_FREE || dep_status == JOB_STATUS_PROMOTING) {
        u32 expected_status = JOB_STATUS_WAITING_FOR_DEPENDENCY;
        if (atomic_compare_exchange_strong(&job->status, &expected_status, JOB_STATUS_NOT_STARTED)) {
            schedule_job_with_priority(self, job, priority);
        }
        // If CAS failed, promote_dependent_jobs() already claimed us - we're done
    }

    return handle;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlJobHandle fl_jobs_add_job_with_dependency(FlJobsFunc func, void* user_data, FlJobHandle dependency) {
    return fl_jobs_add_job_with_priority_and_dependency(func, user_data, JobPriority_Normal, dependency);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlJobsResult fl_jobs_is_finished(FlJobHandle handle) {
    if (handle == JOB_HANDLE_IMMEDIATE_COMPLETE) {
        return FlJobsResult_Finished;
    }

    Jobs* self = g_global_job_system;

    FL_VALIDATE_RET(self != nullptr, FlJobsResult_Invalid);

    const u64 index = (handle & JOB_HANDLE_INDEX_MASK) >> JOB_HANDLE_INDEX_SHIFT;
    const u64 generation = handle & JOB_HANDLE_GEN_MASK;

    if (index >= atomic_load(&self->total_jobs_allocated)) {
        return FlJobsResult_Invalid;
    }

    JobInfo* job = &self->start_jobs[index];

    // (generation, status) must be read as a pair. A concurrent allocation recycles the slot by bumping
    // the generation and only then republishing status, so a status load straddling a recycle describes
    // the slot's new occupant, not this handle's job. Re-reading the generation after the status and
    // requiring it unchanged rejects those reads. A stale handle settles on Invalid, so there is no retry.
    if (atomic_load_explicit(&job->generation, memory_order_acquire) != generation) {
        return FlJobsResult_Invalid;
    }

    u32 status = atomic_load_explicit(&job->status, memory_order_acquire);

    if (atomic_load_explicit(&job->generation, memory_order_acquire) != generation) {
        return FlJobsResult_Invalid;
    }

    if (status == JOB_STATUS_COMPLETED) {
        // The CAS is the exactly-once gate on the free-list push, which makes repeated calls idempotent
        // and makes a recycle landing between the re-check above and here harmless: the worst case is
        // performing the recycle the new occupant's owner would have performed, never a double push.
        u32 expected = JOB_STATUS_COMPLETED;
        if (atomic_compare_exchange_strong(&job->status, &expected, JOB_STATUS_FREE)) {
            // We successfully transitioned to FREE - add to free list. The CAS guarantees exactly one
            // caller recycles the slot; the push is guarded because any non-worker thread may poll.
            mutex_lock(&self->free_list_mutex);
            job->next = self->first_free_entity;
            self->first_free_entity = job;
            mutex_unlock(&self->free_list_mutex);
        }
        return FlJobsResult_Finished;
    }

    if (status == JOB_STATUS_FREE) {
        return FlJobsResult_Finished;
    }

    return FlJobsResult_NotFinished;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_jobs_wait(FlJobHandle handle) {
    if (handle == JOB_HANDLE_IMMEDIATE_COMPLETE) {
        return;
    }

    // A worker must not block here: the waited-on job may need a worker thread to run.
    if (!fl_jobs_is_main_thread()) {
        return;
    }

    while (fl_jobs_is_finished(handle) == FlJobsResult_NotFinished) {
        sleep_us(100);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_jobs_destroy(void) {
    Jobs* self = g_global_job_system;
    FL_VALIDATE(self != nullptr);
    atomic_store(&self->shutdown, 1);

    // Wake up all sleeping workers so they can see the shutdown flag
    mutex_lock(&self->idle_mutex);
    condvar_broadcast(&self->work_available);
    mutex_unlock(&self->idle_mutex);

    for_count(i, self->num_threads) {
        if (self->threads_created[i]) {
            os_thread_join(&self->threads[i]);
        }
    }

    mutex_destroy(&self->free_list_mutex);
    condvar_destroy(&self->work_available);
    mutex_destroy(&self->idle_mutex);

    mutex_destroy(&self->context_mutex);
    if (self->context_arena) {
        fl_arena_destroy(self->context_arena);
        self->context_arena = nullptr;
    }

    if (self->arena) {
        fl_arena_destroy(self->arena);
        self->arena = nullptr;
    }

    g_global_job_system = nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool jobs_global_exists(void) {
    return atomic_load(&g_global_job_system) != nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int fl_jobs_num_threads(void) {
    Jobs* self = g_global_job_system;
    FL_VALIDATE_RET(self != nullptr, 0);
    return self->num_threads;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void* fl_jobs_alloc_context(size_t size) {
    Jobs* self = g_global_job_system;

    FL_VALIDATE_RET(self != nullptr, nullptr);
    FL_VALIDATE_RET(size <= sizeof(JobContext), nullptr);

    mutex_lock(&self->context_mutex);
    JobContext* context = pool_alloc(&self->context_pool);
    mutex_unlock(&self->context_mutex);

    return context;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_jobs_is_main_thread(void) {
    return s_is_main_thread;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static JobInfo* get_job_from_handle(FlJobHandle handle) {
    if (handle == 0 || handle == JOB_HANDLE_IMMEDIATE_COMPLETE) {
        return nullptr;
    }

    Jobs* self = g_global_job_system;

    FL_VALIDATE_RET(self != nullptr, nullptr);

    const u64 index = (handle & JOB_HANDLE_INDEX_MASK) >> JOB_HANDLE_INDEX_SHIFT;
    const u64 generation = handle & JOB_HANDLE_GEN_MASK;

    if (index >= atomic_load(&self->total_jobs_allocated)) {
        return nullptr;
    }

    JobInfo* job = &self->start_jobs[index];

    if (atomic_load(&job->generation) != generation) {
        return nullptr; // Handle has been recycled
    }

    return job;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_jobs_set_priority(FlJobHandle handle, JobPriority new_priority) {
    Jobs* self = g_global_job_system;
    new_priority = clamp_priority(new_priority);

    FL_VALIDATE_RET(self != nullptr, false);

    JobInfo* job = get_job_from_handle(handle);
    if (!job) {
        return false;
    }

    u32 status = atomic_load(&job->status);
    if (status != JOB_STATUS_NOT_STARTED && status != JOB_STATUS_WAITING_FOR_DEPENDENCY) {
        return false; // Too late - job already running or done
    }

    u32 old_priority = atomic_load(&job->priority);
    if (old_priority == (u32)new_priority) {
        return true;
    }

    // Update the priority field FIRST: this marks the old queue entry as stale
    atomic_store(&job->priority, (u32)new_priority);

    // Re-check status AFTER updating priority to close the race window
    // A worker might have claimed the job between our initial check and now
    status = atomic_load(&job->status);
    if (status == JOB_STATUS_NOT_STARTED) {
        // The old entry remains but will be skipped by workers (stale entry detection)
        schedule_job_with_priority(self, job, new_priority);
    }

    // For WAITING_FOR_DEPENDENCY jobs, priority takes effect when promoted
    // (no requeue needed - they're not in any work queue yet)

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlJobPriority fl_jobs_get_priority(FlJobHandle handle) {
    JobInfo* job = get_job_from_handle(handle);
    if (!job) {
        return FlJobPriority_Invalid;
    }

    return (FlJobPriority)atomic_load(&job->priority);
}
