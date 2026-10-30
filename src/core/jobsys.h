#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Job system - internal include seam over the generated <flowi/core/jobs.h>.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <flowi/core/jobs.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct Jobs Jobs;

extern _Atomic(Jobs*) g_global_job_system;

// True once fl_jobs_create_default has installed the global job system, false before it and after teardown.
bool jobs_global_exists(void);

// Special job handle value indicating immediate completion (job executed synchronously)
// Used when scheduling a job from a worker thread - job executes immediately and returns this sentinel
#define JOB_HANDLE_IMMEDIATE_COMPLETE ((FlJobHandle)0xFFFFFFFFFFFFFFFFULL)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Internal job priority spelling (the generated FlJobPriority, named without the Fl prefix for internal call sites)

typedef FlJobPriority JobPriority;

#define JobPriority_Low FlJobPriority_Low
#define JobPriority_Normal FlJobPriority_Normal
#define JobPriority_High FlJobPriority_High

#define JOB_PRIORITY_COUNT 3

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define jobs_context_alloc(Type) ((Type*)fl_jobs_alloc_context(sizeof(Type)))

// Return a context to the pool. The job system calls this automatically on a job's user_data once the job
// function returns; call it directly only for a context that never reached fl_jobs_add_job. Pointers outside
// the context pool are ignored.
void jobs_context_free(void* context);
