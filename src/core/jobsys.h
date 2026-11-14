#pragma once

// Internal include for the job system, layered over the generated <flowi/core/jobs.h>.

#include "types.h"
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <flowi/core/jobs.h>

typedef struct Jobs Jobs;

extern _Atomic(Jobs*) g_global_job_system;

// True once fl_jobs_create has installed the global job system, false before it and after teardown.
bool jobs_global_exists(void);

// Handle returned when the job already ran. Scheduling from a worker thread runs the job
// synchronously and returns this.
#define JOB_HANDLE_IMMEDIATE_COMPLETE ((FlJobHandle)0xFFFFFFFFFFFFFFFFULL)

// The generated FlJobPriority, named without the Fl prefix for internal call sites.

typedef FlJobPriority JobPriority;

#define JobPriority_Low FlJobPriority_Low
#define JobPriority_Normal FlJobPriority_Normal
#define JobPriority_High FlJobPriority_High

#define JOB_PRIORITY_COUNT 3

#define jobs_context_alloc(Type) ((Type*)fl_jobs_alloc_context(sizeof(Type)))

// Return a context to the pool. The job system calls this automatically on a job's user_data once the job
// function returns; call it directly only for a context that never reached fl_jobs_add_job. Pointers outside
// the context pool are ignored.
void jobs_context_free(void* context);
