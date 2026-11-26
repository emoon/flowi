//! Safe front-end over flowi core's work-stealing job system (`fl_jobs_*`).
//!
//! [`Jobs::add`] boxes a Rust closure, passes it through an extern "C" trampoline
//! that reclaims and runs it exactly once, and returns a [`JobHandle`] to query or
//! wait on.
//!
//! A scheduled closure runs on a worker thread at an unknown later time, so it must
//! be Send + 'static and may not borrow from the call site. A panic inside it is
//! caught at the FFI boundary and logged at error level; the job does not finish its
//! work and the process keeps running.
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
use std::ptr;
use std::sync::atomic::{AtomicU8, Ordering};

/// The scheduled-job handle. Query it with
/// [`Jobs::is_finished`], block on it with [`Jobs::wait`], or pass it as a dependency
/// to [`Jobs::add_after`]. [`JobHandle::INVALID`] (0) is the "none" sentinel.
pub use sys::JobHandle;

/// Job scheduling priority (Low / Normal / High). Higher-priority jobs are dequeued
/// first; [`Jobs::add`] uses Normal.
pub use sys::JobPriority;

/// What [`Jobs::is_finished`] reports for a handle: NotFinished (running/pending),
/// Finished (completed), or Invalid (unknown/expired handle).
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

const JOB_RUNNING: u8 = 0;
const JOB_DONE: u8 = 1;
const JOB_ABANDONED: u8 = 2;

struct JobSlot<T> {
    state: AtomicU8,
    completed: bool,
    data: T,
}

struct SendPtr<T>(*mut JobSlot<T>);

// SAFETY: `SendPtr<T>` transfers unique write ownership of `JobSlot<T>` from the
// scheduling thread to the worker thread; `T: Send` allows crossing threads.
unsafe impl<T: Send> Send for SendPtr<T> {}

/// Owning handle to a pre-allocated `T` being mutated in place by a worker job.
///
/// Created by [`Jobs::run`] (and its priority/dependency variants). Moving `T`
/// into the job prevents the caller from touching it while the worker runs,
/// with no `Mutex`, `Arc`, or channel. Once [`is_finished`](Self::is_finished)
/// is true, [`try_take`](Self::try_take) or [`wait`](Self::wait) returns the
/// mutated `T`.
///
/// Dropping a `Job<T>` before taking its value waits for completion on the main
/// thread (or delegates cleanup to the worker if dropped off the main thread)
/// and drops `T` without leaking or dangling.
pub struct Job<T> {
    handle: JobHandle,
    slot: *mut JobSlot<T>,
}

// SAFETY: `Job<T>` uniquely owns the heap-allocated `JobSlot<T>` once the worker
// finishes (coordinated through `JobSlot::state`), and `T: Send` permits moving
// the handle across threads.
unsafe impl<T: Send> Send for Job<T> {}

impl<T> Job<T> {
    /// Underlying [`JobHandle`], for chaining dependents via [`Jobs::add_after`]
    /// or [`Jobs::run_after`].
    #[inline]
    pub fn handle(&self) -> JobHandle {
        self.handle
    }

    /// True once the worker job has finished executing (or its data was already taken).
    #[inline]
    pub fn is_finished(&self) -> bool {
        self.slot.is_null() || Jobs::is_finished(self.handle) != JobsResult::NotFinished
    }

    /// Reclaim the pre-allocated `T` if the job has finished without panicking.
    ///
    /// Returns `None` while the job is still running, if the value was already
    /// taken, or if the job closure panicked (in which case [`is_finished`](Self::is_finished)
    /// is true and `T` is dropped cleanly).
    pub fn try_take(&mut self) -> Option<T> {
        if self.slot.is_null() || !self.is_finished() {
            return None;
        }
        self.reclaim_finished_slot()
    }

    /// Block on the main thread until the job completes and return `T`, or
    /// `None` if the job closure panicked (or the value was already taken).
    ///
    /// # Panics
    /// Panics if called off the main thread, because `fl_jobs_wait` cannot block
    /// a worker thread.
    pub fn wait(mut self) -> Option<T> {
        assert!(
            Jobs::is_main_thread(),
            "flowi: Job::wait may only be called on the main thread"
        );
        if self.slot.is_null() {
            return None;
        }
        Jobs::wait(self.handle);
        self.reclaim_finished_slot()
    }

    fn reclaim_finished_slot(&mut self) -> Option<T> {
        let ptr = self.slot;
        self.slot = ptr::null_mut();
        // SAFETY: callers verified completion through `Jobs::is_finished` or
        // `Jobs::wait`, whose acquire load of `JOB_STATUS_COMPLETED` synchronises
        // with the worker's release store after `execute_owned_slot` returned.
        let slot = unsafe { Box::from_raw(ptr) };
        slot.completed.then_some(slot.data)
    }
}

impl<T> Drop for Job<T> {
    fn drop(&mut self) {
        if self.slot.is_null() {
            return;
        }
        if Jobs::is_main_thread() {
            Jobs::wait(self.handle);
            let _ = self.reclaim_finished_slot();
            return;
        }
        let ptr = self.slot;
        self.slot = ptr::null_mut();
        // SAFETY: `ptr` is live because neither this handle nor the worker has freed it
        // yet. Swapping to `JOB_ABANDONED` lets the worker free it if still running, or
        // lets us free it immediately if the worker already stored `JOB_DONE`.
        let prev = unsafe { (*ptr).state.swap(JOB_ABANDONED, Ordering::AcqRel) };
        if prev == JOB_DONE {
            // SAFETY: `prev == JOB_DONE` with Acquire ordering guarantees the worker
            // observed `JOB_RUNNING` and will never access or free `ptr` again.
            unsafe { drop(Box::from_raw(ptr)) };
        }
    }
}

fn execute_owned_slot<T, F>(slot_ptr: SendPtr<T>, f: F, worker: WorkerInfo)
where
    T: Send + 'static,
    F: FnOnce(&mut T, WorkerInfo) + Send + 'static,
{
    let ptr = slot_ptr.0;
    let result = catch_unwind(AssertUnwindSafe(|| {
        // SAFETY: `ptr` was created by `Box::into_raw` in `schedule_owned` and stays
        // exclusively writable by this worker until it transitions `state` to `JOB_DONE`.
        let data = unsafe { &mut (*ptr).data };
        f(data, worker);
    }));
    let completed = match result {
        Ok(()) => true,
        Err(payload) => {
            log::error!(
                "job panicked on worker {}: {}",
                worker.worker_index,
                sys::guard::payload_message(&*payload)
            );
            false
        }
    };
    // SAFETY: no other thread reads `completed` before observing `JOB_DONE`, and
    // `ptr` stays alive until the atomic swap below decides who frees it.
    let prev = unsafe {
        (*ptr).completed = completed;
        (*ptr).state.swap(JOB_DONE, Ordering::AcqRel)
    };
    if prev == JOB_ABANDONED {
        // SAFETY: the handle abandoned `ptr` while `JOB_RUNNING` and transferred
        // reclamation to this worker; no other reference to `ptr` remains.
        unsafe { drop(Box::from_raw(ptr)) };
    }
}

fn schedule_owned<T, F>(data: T, priority: JobPriority, dependency: JobHandle, f: F) -> Job<T>
where
    T: Send + 'static,
    F: FnOnce(&mut T, WorkerInfo) + Send + 'static,
{
    let slot = Box::into_raw(Box::new(JobSlot {
        state: AtomicU8::new(JOB_RUNNING),
        completed: false,
        data,
    }));
    let send_ptr = SendPtr(slot);
    let handle = Jobs::add_with_priority_after(
        move |worker| execute_owned_slot(send_ptr, f, worker),
        priority,
        dependency,
    );
    Job { handle, slot }
}

impl Jobs {
    /// Schedule f to run on a worker thread at Normal priority; returns its
    /// [`JobHandle`]. The closure runs once, later, on another thread, so it must be
    /// Send + 'static. See the module docs for the panic contract.
    ///
    /// # Panics
    /// Panics if the foundation is not up; see the module docs on lifecycle.
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
    /// Panics if the foundation is not up; see the module docs on lifecycle.
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
    /// a job body against a dependency that is still running, f is deferred, so
    /// ordering holds on a worker too, but the returned handle is then pending and
    /// only the main thread can wait on it.
    ///
    /// # Panics
    /// Panics if the foundation is not up; see the module docs on lifecycle.
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
    /// Panics if the foundation is not up; see the module docs on lifecycle.
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

    /// Schedule `f` to mutate a pre-allocated `data` in place on a worker thread
    /// at Normal priority, returning an owning [`Job<T>`] handle.
    ///
    /// # Panics
    /// Panics if the foundation is not up; see the module docs on lifecycle.
    #[inline]
    pub fn run<T, F>(data: T, f: F) -> Job<T>
    where
        T: Send + 'static,
        F: FnOnce(&mut T, WorkerInfo) + Send + 'static,
    {
        schedule_owned(data, JobPriority::Normal, JobHandle::INVALID, f)
    }

    /// Schedule `f` to mutate `data` in place at an explicit [`JobPriority`].
    ///
    /// # Panics
    /// Panics if the foundation is not up; see the module docs on lifecycle.
    #[inline]
    pub fn run_with_priority<T, F>(data: T, priority: JobPriority, f: F) -> Job<T>
    where
        T: Send + 'static,
        F: FnOnce(&mut T, WorkerInfo) + Send + 'static,
    {
        schedule_owned(data, priority, JobHandle::INVALID, f)
    }

    /// Schedule `f` to mutate `data` in place once `dependency` completes
    /// (Normal priority).
    ///
    /// # Panics
    /// Panics if the foundation is not up; see the module docs on lifecycle.
    #[inline]
    pub fn run_after<T, F>(data: T, dependency: JobHandle, f: F) -> Job<T>
    where
        T: Send + 'static,
        F: FnOnce(&mut T, WorkerInfo) + Send + 'static,
    {
        schedule_owned(data, JobPriority::Normal, dependency, f)
    }

    /// Schedule `f` to mutate `data` in place at `priority` once `dependency`
    /// completes.
    ///
    /// # Panics
    /// Panics if the foundation is not up; see the module docs on lifecycle.
    #[inline]
    pub fn run_with_priority_after<T, F>(
        data: T,
        priority: JobPriority,
        dependency: JobHandle,
        f: F,
    ) -> Job<T>
    where
        T: Send + 'static,
        F: FnOnce(&mut T, WorkerInfo) + Send + 'static,
    {
        schedule_owned(data, priority, dependency, f)
    }

    /// Whether handle has finished; see [`JobsResult`]. An unknown/expired handle
    /// reports [`JobsResult::Invalid`].
    #[inline]
    pub fn is_finished(handle: JobHandle) -> JobsResult {
        // SAFETY: a plain query over a Copy handle value; no pointers cross.
        unsafe { sys::fl_jobs_is_finished(handle) }
    }

    /// Block the calling thread until handle completes, on the main thread only.
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
    /// Panics if the foundation is not up; see the module docs on lifecycle.
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

    #[test]
    fn owned_job_mutates_preallocated_data_in_place() {
        foundation();
        let preallocated = vec![10u32, 20, 30];
        let mut job = Jobs::run(preallocated, |values, _worker| {
            for v in values.iter_mut() {
                *v *= 2;
            }
            values.push(80);
        });

        while !job.is_finished() {
            std::thread::yield_now();
        }
        assert!(job.is_finished());
        let result = job.try_take().unwrap();
        assert_eq!(result, vec![20, 40, 60, 80]);
        assert!(job.try_take().is_none(), "value is yielded once");
    }

    #[test]
    fn owned_job_wait_and_dependency_chain() {
        foundation();
        let first = Jobs::run(String::from("hello"), |text, _| text.push_str(", "));
        let second = Jobs::run_after(String::from("world"), first.handle(), |text, _| {
            text.push('!');
        });
        let a = first.wait().unwrap();
        let b = second.wait().unwrap();
        assert_eq!(format!("{a}{b}"), "hello, world!");
    }

    #[test]
    fn owned_job_panic_drops_data_and_returns_none() {
        foundation();
        let dropped = Arc::new(AtomicBool::new(false));
        struct DropFlag(Arc<AtomicBool>);
        impl Drop for DropFlag {
            fn drop(&mut self) {
                self.0.store(true, Ordering::Release);
            }
        }

        let job = Jobs::run(DropFlag(Arc::clone(&dropped)), |_, _| {
            panic!("deliberate owned job panic");
        });
        assert!(job.wait().is_none());
        assert!(dropped.load(Ordering::Acquire), "data dropped after panic");
    }

    #[test]
    fn dropping_owned_job_off_main_thread_delegates_cleanup_to_worker() {
        foundation();
        let dropped = Arc::new(AtomicBool::new(false));
        struct DropFlag(Arc<AtomicBool>);
        impl Drop for DropFlag {
            fn drop(&mut self) {
                self.0.store(true, Ordering::Release);
            }
        }

        let flag = Arc::clone(&dropped);
        let spawner = Jobs::add(move |_| {
            let child = Jobs::run(DropFlag(flag), |_, _| {
                std::thread::sleep(std::time::Duration::from_millis(10));
            });
            drop(child);
        });
        Jobs::wait(spawner);

        let deadline = std::time::Instant::now() + std::time::Duration::from_secs(2);
        while std::time::Instant::now() < deadline && !dropped.load(Ordering::Acquire) {
            std::thread::sleep(std::time::Duration::from_millis(2));
        }
        assert!(
            dropped.load(Ordering::Acquire),
            "abandoned job's data was reclaimed by its worker"
        );
    }
}
