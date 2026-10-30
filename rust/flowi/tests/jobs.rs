//! Scheduling calls do not bootstrap the foundation, so every test here brings it up first.

use flowi::{JobHandle, JobPriority, Jobs, JobsResult, WorkerInfo};
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::sync::{mpsc, Arc, Mutex};

/// Idempotent, so each test in this binary calls it.
fn foundation() {
    let _arena = flowi::init(1);
}

#[test]
fn job_runs_closure_exactly_once_and_waits() {
    foundation();
    let counter = Arc::new(AtomicUsize::new(0));
    let worker = Arc::new(AtomicUsize::new(usize::MAX));

    let c = counter.clone();
    let w = worker.clone();
    let handle: JobHandle = Jobs::add(move |info: WorkerInfo| {
        c.fetch_add(1, Ordering::SeqCst);
        w.store(info.worker_index as usize, Ordering::SeqCst);
    });

    Jobs::wait(handle);

    // The trampoline reclaims the box after the single call.
    assert_eq!(
        counter.load(Ordering::SeqCst),
        1,
        "closure should run exactly once"
    );

    let idx = worker.load(Ordering::SeqCst);
    let threads = Jobs::num_threads() as usize;
    assert!(threads >= 1, "job system should have at least one worker");
    assert!(
        idx < threads,
        "worker index {idx} out of range 0..{threads}"
    );
}

#[test]
fn dependency_orders_two_jobs() {
    foundation();
    let order = Arc::new(AtomicUsize::new(0));

    let o1 = order.clone();
    let first = Jobs::add(move |_| {
        let _ = o1.compare_exchange(0, 1, Ordering::SeqCst, Ordering::SeqCst);
    });

    let o2 = order.clone();
    let second = Jobs::add_after(
        move |_| {
            // Only advances to 2 if first already set 1.
            let _ = o2.compare_exchange(1, 2, Ordering::SeqCst, Ordering::SeqCst);
        },
        first,
    );

    Jobs::wait(second);
    assert_eq!(
        order.load(Ordering::SeqCst),
        2,
        "dependent job must run after its dependency"
    );
}

#[test]
fn query_surface_is_safe() {
    foundation();
    assert!(Jobs::is_main_thread(), "test thread is the main thread");

    let done = Arc::new(AtomicUsize::new(0));
    let d = done.clone();
    let handle = Jobs::add(move |_| {
        d.fetch_add(1, Ordering::SeqCst);
    });
    Jobs::wait(handle);

    // After completion the handle reports Finished, or Invalid once recycled.
    assert!(
        matches!(
            Jobs::is_finished(handle),
            JobsResult::Finished | JobsResult::Invalid
        ),
        "a waited-on job is not still pending"
    );

    assert!(
        !Jobs::set_priority(JobHandle::INVALID, JobPriority::Low),
        "an invalid handle cannot be reprioritized"
    );
    assert_eq!(
        Jobs::get_priority(JobHandle::INVALID),
        JobPriority::Invalid,
        "an invalid handle has no priority"
    );
}

#[test]
fn add_with_priority_runs_the_closure() {
    foundation();
    let ran = Arc::new(AtomicUsize::new(0));
    let r = ran.clone();
    let handle = Jobs::add_with_priority(
        move |_| {
            r.fetch_add(1, Ordering::SeqCst);
        },
        JobPriority::High,
    );
    Jobs::wait(handle);
    assert_eq!(ran.load(Ordering::SeqCst), 1, "high-priority job ran once");
}

#[test]
fn panicking_closure_is_contained() {
    foundation();
    // The panic is contained without unwinding across the C boundary; its stderr message
    // is expected.
    let panicked = Arc::new(AtomicUsize::new(0));
    let p = panicked.clone();
    let bad = Jobs::add(move |_| {
        p.fetch_add(1, Ordering::SeqCst);
        panic!("job blew up on purpose");
    });
    Jobs::wait(bad);
    assert_eq!(
        panicked.load(Ordering::SeqCst),
        1,
        "the panicking closure did execute"
    );

    let ok = Arc::new(AtomicUsize::new(0));
    let o = ok.clone();
    let good = Jobs::add(move |_| {
        o.fetch_add(1, Ordering::SeqCst);
    });
    Jobs::wait(good);
    assert_eq!(
        ok.load(Ordering::SeqCst),
        1,
        "the worker survived the panic and ran the next job"
    );
}

/// A dependent submitted from inside a job body must wait for its prerequisite rather than run
/// inline. The prerequisite is the submitting job itself, which makes the ordering observable with
/// a single worker.
#[test]
fn worker_submitted_dependent_waits_for_its_prerequisite() {
    foundation();

    // The submitting job needs its own handle, which only exists once add returns.
    let own_handle: Arc<Mutex<Option<JobHandle>>> = Arc::new(Mutex::new(None));
    let submitter_returned = Arc::new(AtomicBool::new(false));
    let saw_prerequisite_finish = Arc::new(AtomicBool::new(false));
    let ran = Arc::new(AtomicUsize::new(0));
    let (tx, rx) = mpsc::channel();

    let published = Arc::clone(&own_handle);
    let returned = Arc::clone(&submitter_returned);
    let observed = Arc::clone(&submitter_returned);
    let saw = Arc::clone(&saw_prerequisite_finish);
    let count = Arc::clone(&ran);
    let submitter = Jobs::add(move |_| {
        let dependency = loop {
            let handle = *published.lock().expect("the handle slot is never poisoned");
            if let Some(handle) = handle {
                break handle;
            }
            std::thread::yield_now();
        };
        let child = Jobs::add_after(
            move |_| {
                // Set at the end of the prerequisite's body, so it is false for a
                // child that ran inline and true for one promoted afterwards.
                saw.store(observed.load(Ordering::Acquire), Ordering::Release);
                count.fetch_add(1, Ordering::AcqRel);
            },
            dependency,
        );
        let _ = tx.send(child);
        returned.store(true, Ordering::Release);
    });
    *own_handle
        .lock()
        .expect("the handle slot is never poisoned") = Some(submitter);

    Jobs::wait(submitter);
    let child = rx
        .recv()
        .expect("the submitting job published its child handle");
    Jobs::wait(child);

    assert_eq!(
        ran.load(Ordering::Acquire),
        1,
        "the dependent ran exactly once"
    );
    assert!(
        saw_prerequisite_finish.load(Ordering::Acquire),
        "the dependent ran inside its prerequisite instead of after it"
    );
}

/// A settled dependency leaves nothing to order, so its dependent runs either way. Which way is not
/// asserted: waiting on the dependency frees its slot, so whether the add below sees an expired
/// handle (runs inline) or a settled one (schedules) turns on who claimed that slot.
#[test]
fn worker_submitted_dependent_on_a_settled_dependency_runs() {
    foundation();

    let settled = Jobs::add(|_| {});
    Jobs::wait(settled);

    let ran = Arc::new(AtomicUsize::new(0));
    let (tx, rx) = mpsc::channel();
    let count = Arc::clone(&ran);
    let submitter = Jobs::add(move |_| {
        let child = Jobs::add_after(
            move |_| {
                count.fetch_add(1, Ordering::AcqRel);
            },
            settled,
        );
        let _ = tx.send(child);
    });
    Jobs::wait(submitter);
    Jobs::wait(
        rx.recv()
            .expect("the submitting job published its child handle"),
    );

    assert_eq!(
        ran.load(Ordering::Acquire),
        1,
        "a settled dependency must not strand its dependent"
    );
}

/// The sentinel handle returned for a job that ran inline on the submitting worker.
const IMMEDIATE_COMPLETE: JobHandle = JobHandle(u64::MAX);

/// Chaining off a handle a worker got from its own inline submission is ordinary, and the sentinel
/// that handle carries means "already complete" - so the dependent must still run.
#[test]
fn worker_submitted_dependent_on_the_immediate_complete_sentinel_runs() {
    foundation();

    let inline_ran = Arc::new(AtomicUsize::new(0));
    let dependent_ran = Arc::new(AtomicUsize::new(0));
    let (tx, rx) = mpsc::channel();
    let inline_count = Arc::clone(&inline_ran);
    let dependent_count = Arc::clone(&dependent_ran);
    let submitter = Jobs::add(move |_| {
        let inline = Jobs::add(move |_| {
            inline_count.fetch_add(1, Ordering::AcqRel);
        });
        let dependent = Jobs::add_after(
            move |_| {
                dependent_count.fetch_add(1, Ordering::AcqRel);
            },
            inline,
        );
        let _ = tx.send((inline, dependent));
    });
    Jobs::wait(submitter);

    let (inline, dependent) = rx
        .recv()
        .expect("the submitting job published both handles");
    Jobs::wait(dependent);

    assert_eq!(
        inline, IMMEDIATE_COMPLETE,
        "a plain job submitted from a worker runs inline and reports the sentinel"
    );
    assert_eq!(
        inline_ran.load(Ordering::Acquire),
        1,
        "the inline job ran exactly once"
    );
    assert_eq!(
        dependent_ran.load(Ordering::Acquire),
        1,
        "the sentinel dependency stranded its dependent"
    );
}

/// A job body waiting on a handle that cannot be finished returns anyway; the still-NotFinished
/// readback shows it returned without the job having completed.
#[test]
fn wait_from_a_job_body_returns_without_blocking() {
    foundation();

    let own_handle: Arc<Mutex<Option<JobHandle>>> = Arc::new(Mutex::new(None));
    let (tx, rx) = mpsc::channel();

    let published = Arc::clone(&own_handle);
    let waiter = Jobs::add(move |_| {
        let own = loop {
            let handle = *published.lock().expect("the handle slot is never poisoned");
            if let Some(handle) = handle {
                break handle;
            }
            std::thread::yield_now();
        };
        // The job is running, so this handle cannot report Finished until after the body returns.
        Jobs::wait(own);
        let _ = tx.send(Jobs::is_finished(own));
    });
    *own_handle
        .lock()
        .expect("the handle slot is never poisoned") = Some(waiter);

    Jobs::wait(waiter);

    assert!(
        matches!(
            rx.recv().expect("the job body published what it observed"),
            JobsResult::NotFinished
        ),
        "wait on a worker must return without the waited-on job having finished"
    );
}

/// Zero and an expired handle mean "no dependency" on a worker exactly as on the main
/// thread: the job is scheduled, never parked on a list nothing will drain.
#[test]
fn worker_submitted_dependent_on_an_unusable_handle_still_runs() {
    foundation();

    for dependency in [JobHandle::INVALID, JobHandle(u64::MAX - 1)] {
        let ran = Arc::new(AtomicUsize::new(0));
        let (tx, rx) = mpsc::channel();
        let count = Arc::clone(&ran);
        let submitter = Jobs::add(move |_| {
            let child = Jobs::add_after(
                move |_| {
                    count.fetch_add(1, Ordering::AcqRel);
                },
                dependency,
            );
            let _ = tx.send(child);
        });
        Jobs::wait(submitter);
        Jobs::wait(
            rx.recv()
                .expect("the submitting job published its child handle"),
        );

        assert_eq!(
            ran.load(Ordering::Acquire),
            1,
            "an unusable dependency must not strand the job"
        );
    }
}
