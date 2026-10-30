///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Compile-only parity guard: the api_gen-generated public job-system surface (<flowi/core/jobs.h>).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <flowi/core/jobs.h>

// Per-prototype signature identity: a reordered/retyped arg or changed return fails the build.
#define FL_JOBS_FN_MATCHES(fn, ...) \
    _Static_assert(__builtin_types_compatible_p(__typeof__(fn), __VA_ARGS__), #fn " signature drift")

FL_JOBS_FN_MATCHES(fl_jobs_create, void(FlArena*, int32_t));
FL_JOBS_FN_MATCHES(fl_jobs_add_job, FlJobHandle(FlJobsFunc, void*));
FL_JOBS_FN_MATCHES(fl_jobs_add_job_with_dependency, FlJobHandle(FlJobsFunc, void*, FlJobHandle));
FL_JOBS_FN_MATCHES(fl_jobs_is_finished, FlJobsResult(FlJobHandle));
FL_JOBS_FN_MATCHES(fl_jobs_wait, void(FlJobHandle));
FL_JOBS_FN_MATCHES(fl_jobs_destroy, void(void));
FL_JOBS_FN_MATCHES(fl_jobs_num_threads, int32_t(void));
FL_JOBS_FN_MATCHES(fl_jobs_add_job_with_priority, FlJobHandle(FlJobsFunc, void*, FlJobPriority));
FL_JOBS_FN_MATCHES(fl_jobs_add_job_with_priority_and_dependency,
                   FlJobHandle(FlJobsFunc, void*, FlJobPriority, FlJobHandle));
FL_JOBS_FN_MATCHES(fl_jobs_set_priority, bool(FlJobHandle, FlJobPriority));
FL_JOBS_FN_MATCHES(fl_jobs_get_priority, FlJobPriority(FlJobHandle));
FL_JOBS_FN_MATCHES(fl_jobs_alloc_context, void*(size_t));
FL_JOBS_FN_MATCHES(fl_jobs_is_main_thread, bool(void));

#undef FL_JOBS_FN_MATCHES

_Static_assert(sizeof(FlJobHandle) == 8, "FlJobHandle size drift");
_Static_assert(FL_JOB_HANDLE_INVALID == 0, "FlJobHandle invalid sentinel drift");

// The result / priority enums cross the ABI by value, so pin their size and enumerator values.
_Static_assert(sizeof(FlJobsResult) == 4, "FlJobsResult size drift");
_Static_assert(FlJobsResult_NotFinished == 0 && FlJobsResult_Finished == 1 && FlJobsResult_Invalid == 2,
               "FlJobsResult value drift");
_Static_assert(sizeof(FlJobPriority) == 4, "FlJobPriority size drift");
_Static_assert(FlJobPriority_Low == 0 && FlJobPriority_Normal == 1 && FlJobPriority_High == 2
                   && FlJobPriority_Invalid == -1,
               "FlJobPriority value drift");

// FlJobsWorkerInfo POD layout: the int worker index, 4 bytes of padding, then the 8-byte scratch arena pointer.
_Static_assert(sizeof(FlJobsWorkerInfo) == 16, "FlJobsWorkerInfo size drift");
_Static_assert(_Alignof(FlJobsWorkerInfo) == 8, "FlJobsWorkerInfo align drift");
_Static_assert(offsetof(FlJobsWorkerInfo, worker_index) == 0, "FlJobsWorkerInfo.worker_index offset drift");
_Static_assert(offsetof(FlJobsWorkerInfo, scratch) == 8, "FlJobsWorkerInfo.scratch offset drift");
