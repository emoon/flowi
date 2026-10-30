//! Safe front-end over flowi core's work-stealing job system (`fl_jobs_*`).
//!
//! [`Jobs::add`] boxes a Rust closure, passes it through an extern "C" trampoline
//! that reclaims and runs it exactly once, and returns a [`JobHandle`] to query or
//! wait on.
//!
//! A scheduled closure runs on a worker thread at an unknown later time, so it must
//! be Send + 'static and may not borrow from the call site. A panic inside it is
//! caught at the FFI boundary and logged at error level; the job fails to finish its
//! work rather than aborting the process.
//!
//! Only the main thread can block on a handle. [`Jobs::wait`] returns straight away
//! everywhere else, so a job body that needs ordering expresses it with
//! [`Jobs::add_after`] rather than by waiting.
//!
//! These calls operate on the process-global job system and do not bring it up:
//! [`crate::init`] (or any flowi entry point that starts the foundation) must have run
//! first, and scheduling before then panics.

use crate::sys;
use std::os::raw::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};

/// The scheduled-job handle, re-exported under its clean name. Query it with
/// [`Jobs::is_finished`], block on it with [`Jobs::wait`], or pass it as a dependency
/// to [`Jobs::add_after`]. [`JobHandle::INVALID`] (0) is the "none" sentinel.
pub use sys::JobHandle;

/// Job scheduling priority (Low / Normal / High), re-exported under its clean
/// name. Higher-priority jobs are dequeued first; [`Jobs::add`] uses Normal.
pub use sys::JobPriority;

/// What [`Jobs::is_finished`] reports for a handle: NotFinished (running/pending),
/// Finished (completed), or Invalid (unknown/expired handle). Re-exported under
/// its clean name.
pub use sys::JobsResult;

/// The worker-thread context handed to a running job.
#[derive(Copy, Clone, Debug)]
pub struct WorkerInfo {
    /// Index of the worker thread running this job (0..Jobs::num_threads()).
    pub worker_index: i32,
}

/// The extern "C" shim every scheduled closure runs through: reclaims the boxed closure
/// and runs it inside catch_unwind.
extern "C" fn trampoline<F>(user_data: *mut c_void, info: sys::JobsWorkerInfo)
where
    F: FnOnce(WorkerInfo) + Send + 'static,
{
    // SAFETY: user_data is the `Box::into_raw(Box::<F>::new(..))` pointer the matching
    // `Jobs::add*` produced; the job system calls this trampoline exactly once for it.
    let closure = unsafe { Box::from_raw(user_data as *mut F) };
    let worker = WorkerInfo {
        worker_index: info.worker_index,
    };
    if let Err(payload) = catch_unwind(AssertUnwindSafe(move || closure(worker))) {
        log::error!(
            "job panicked on worker {}: {}",
            worker.worker_index,
            sys::guard::payload_message(&*payload)
        );
    }
}

/// Scheduling and query functions over the process-global job system.
pub struct Jobs;

/// The message every scheduling entry point asserts with.
const FOUNDATION_DOWN: &str =
    "flowi: the job system is not up — start the foundation first (Application::new or flowi::init)";

impl Jobs {
    /// Schedule f to run on a worker thread at Normal priority; returns its
    /// [`JobHandle`]. The closure runs once, later, on another thread, so it must be
    /// Send + 'static. See the module docs for the panic contract.
    ///
    /// # Panics
    /// Panics if the foundation is not up - see the module docs on lifecycle.
    #[inline]
    pub fn add<F>(f: F) -> JobHandle
    where
        F: FnOnce(WorkerInfo) + Send + 'static,
    {
        Self::add_with_priority(f, JobPriority::Normal)
    }

    /// Schedule f at an explicit [`JobPriority`]; returns its [`JobHandle`]. See
    /// [`Jobs::add`] for the closure contract.
    ///
    /// # Panics
    /// Panics if the foundation is not up - see the module docs on lifecycle.
    pub fn add_with_priority<F>(f: F, priority: JobPriority) -> JobHandle
    where
        F: FnOnce(WorkerInfo) + Send + 'static,
    {
        assert!(crate::foundation::is_up(), "{FOUNDATION_DOWN}");
        let user_data = Box::into_raw(Box::new(f)) as *mut c_void;
        // SAFETY: `trampoline::<F>` is the exact shim for this boxed F, and the assert above
        // established the global job system is live. The trampoline reclaims the box.
        unsafe { sys::fl_jobs_add_job_with_priority(Some(trampoline::<F>), user_data, priority) }
    }

    /// Schedule f to run only after dependency completes (Normal priority);
    /// returns its [`JobHandle`]. See [`Jobs::add`] for the closure contract.
    ///
    /// [`JobHandle::INVALID`], an expired handle, and one whose job has already
    /// finished all count as "no dependency", so f is simply scheduled. Called from
    /// a job body against a dependency that is still running, f is deferred rather
    /// than run inline - ordering holds on a worker too, but the returned handle is
    /// then genuinely pending and only the main thread can wait it out.
    ///
    /// # Panics
    /// Panics if the foundation is not up - see the module docs on lifecycle.
    pub fn add_after<F>(f: F, dependency: JobHandle) -> JobHandle
    where
        F: FnOnce(WorkerInfo) + Send + 'static,
    {
        assert!(crate::foundation::is_up(), "{FOUNDATION_DOWN}");
        let user_data = Box::into_raw(Box::new(f)) as *mut c_void;
        // SAFETY: as in add_with_priority.
        unsafe {
            sys::fl_jobs_add_job_with_dependency(Some(trampoline::<F>), user_data, dependency)
        }
    }

    /// Schedule f at priority, to run only after dependency completes; returns
    /// its [`JobHandle`]. See [`Jobs::add_after`] for how the dependency is honored
    /// and [`Jobs::add`] for the closure contract.
    ///
    /// # Panics
    /// Panics if the foundation is not up - see the module docs on lifecycle.
    pub fn add_with_priority_after<F>(
        f: F,
        priority: JobPriority,
        dependency: JobHandle,
    ) -> JobHandle
    where
        F: FnOnce(WorkerInfo) + Send + 'static,
    {
        assert!(crate::foundation::is_up(), "{FOUNDATION_DOWN}");
        let user_data = Box::into_raw(Box::new(f)) as *mut c_void;
        // SAFETY: as in add_with_priority.
        unsafe {
            sys::fl_jobs_add_job_with_priority_and_dependency(
                Some(trampoline::<F>),
                user_data,
                priority,
                dependency,
            )
        }
    }

    /// Whether handle has finished - see [`JobsResult`]. An unknown/expired handle
    /// reports [`JobsResult::Invalid`].
    #[inline]
    pub fn is_finished(handle: JobHandle) -> JobsResult {
        // SAFETY: a plain query over a Copy handle value; no pointers cross.
        unsafe { sys::fl_jobs_is_finished(handle) }
    }

    /// Block the calling thread until handle completes - on the main thread only.
    /// From a job body or any other thread this returns at once without waiting;
    /// poll [`Jobs::is_finished`] there, or chain the work with [`Jobs::add_after`].
    #[inline]
    pub fn wait(handle: JobHandle) {
        // SAFETY: a plain query over a Copy handle value; blocking on the main thread only.
        unsafe { sys::fl_jobs_wait(handle) }
    }

    /// Change a still-pending job's priority. Returns false if it already
    /// started/completed or the handle is invalid.
    #[inline]
    pub fn set_priority(handle: JobHandle, priority: JobPriority) -> bool {
        // SAFETY: a plain mutator over Copy values; no pointers cross.
        unsafe { sys::fl_jobs_set_priority(handle, priority) }
    }

    /// The current priority of a job, or [`JobPriority::Invalid`] if the handle is
    /// invalid.
    #[inline]
    pub fn get_priority(handle: JobHandle) -> JobPriority {
        // SAFETY: a plain query over a Copy handle value.
        unsafe { sys::fl_jobs_get_priority(handle) }
    }

    /// The number of worker threads in the global job system.
    ///
    /// # Panics
    /// Panics if the foundation is not up - see the module docs on lifecycle.
    pub fn num_threads() -> i32 {
        assert!(crate::foundation::is_up(), "{FOUNDATION_DOWN}");
        // SAFETY: no arguments; reads a global the assert above established is live.
        unsafe { sys::fl_jobs_num_threads() }
    }

    /// Whether the caller is executing on the main thread (rather than a worker).
    #[inline]
    pub fn is_main_thread() -> bool {
        // SAFETY: no arguments; reads thread-local state.
        unsafe { sys::fl_jobs_is_main_thread() }
    }
}

#[cfg(test)]
#[allow(clippy::unwrap_used, clippy::expect_used)]
mod tests {
    use super::*;
    use crate::Arena;
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::sync::mpsc;
    use std::sync::Arc;

    /// Bring the foundation up; idempotent.
    fn foundation() {
        let _arena = crate::init(1);
    }

    #[test]
    fn a_job_body_allocates_from_the_worker_scratch_arena() {
        foundation();
        let (tx, rx) = mpsc::channel();
        let handle = Jobs::add(move |_worker| {
            let echoed = Arena::scratch(|scratch| {
                let text = scratch.alloc_str("scratch is live");
                let numbers = scratch.alloc_slice_copy(&[1u32, 2, 3]);
                (text.to_string(), numbers.iter().sum::<u32>())
            });
            // Nested scopes share the pool.
            let nested = Arena::scratch(|outer| {
                let first = outer.alloc_str("outer").to_string();
                let second = Arena::scratch(|inner| inner.alloc_str("inner").to_string());
                format!("{first}/{second}")
            });
            tx.send((echoed, nested, Jobs::is_main_thread())).unwrap();
        });
        Jobs::wait(handle);

        let ((text, sum), nested, on_main_thread) = rx.recv().unwrap();
        assert_eq!(text, "scratch is live");
        assert_eq!(sum, 6);
        assert_eq!(nested, "outer/inner");
        assert!(
            !on_main_thread,
            "a job body runs on a worker, not the main thread"
        );
    }

    #[test]
    fn a_panicking_job_is_contained_and_the_pool_keeps_working() {
        foundation();
        let panicked = Arc::new(AtomicBool::new(false));
        let flag = Arc::clone(&panicked);
        let boom = Jobs::add(move |_| {
            flag.store(true, Ordering::Release);
            panic!("deliberate job panic");
        });
        Jobs::wait(boom);
        assert!(panicked.load(Ordering::Acquire), "the job body ran");

        let ran_after = Arc::new(AtomicBool::new(false));
        let flag = Arc::clone(&ran_after);
        let after = Jobs::add(move |_| flag.store(true, Ordering::Release));
        Jobs::wait(after);
        assert!(
            ran_after.load(Ordering::Acquire),
            "the pool still dispatches after a job panicked"
        );
    }

    /// add_after runs its body only once the dependency has finished.
    #[test]
    fn a_dependent_job_runs_after_its_dependency() {
        foundation();
        let (tx, rx) = mpsc::channel();
        let first_tx = tx.clone();
        let first = Jobs::add(move |_| first_tx.send("first").unwrap());
        let second = Jobs::add_after(move |_| tx.send("second").unwrap(), first);
        Jobs::wait(second);

        assert_eq!(rx.recv().unwrap(), "first");
        assert_eq!(rx.recv().unwrap(), "second");
    }
}
