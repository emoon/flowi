//! A single-threaded, frame-ticked local executor for cooperative multi-step
//! async chains over [`Handle<T>`](crate::Handle).
//!
//! Call [`LocalExecutor::tick`] once per frame from the main loop, so tasks resume on
//! the thread that owns the UI context. The executor and its tasks hold
//! [`Rc`](std::rc::Rc) and are !Send + !Sync.
//!
//! [`spawn`](LocalExecutor::spawn) takes F: Future + 'static: a root task owns its
//! captures and returns its result, so no borrow of a per-frame arena can be held
//! across an await.
//!
//! Dropping the [`Task`] handle cancels: it drops the in-flight future now, which
//! drops its captured [`Handle`](crate::Handle)s and closes their operations
//! without blocking.

use core::cell::RefCell;
use core::future::Future;
use core::mem::ManuallyDrop;
use core::pin::Pin;
use core::sync::atomic::{AtomicBool, Ordering};
use core::task::{Context, RawWaker, RawWakerVTable, Waker};
use std::rc::Rc;
use std::sync::Arc;

/// A type-erased root future the executor drives to (). The concrete output is
/// captured out into the [`Task`]'s shared output cell.
type LocalFuture = Pin<Box<dyn Future<Output = ()>>>;

/// One task's mutable core, shared between the executor's registry and the owning
/// [`Task`] handle. Invariant: the task's [`Waker`](core::task::Waker) holds only the
/// [`ready`](Self::ready) flag, the one piece of state reachable from another thread.
struct TaskInner {
    /// The in-flight future, or [`None`] once it has completed or been cancelled by
    /// a dropped [`Task`]. Taken out of the slot for the duration of its poll, so the
    /// poll can re-enter the executor (spawn a sibling).
    future: RefCell<Option<LocalFuture>>,
    /// Whether the task is scheduled to poll on the next [`tick`](LocalExecutor::tick).
    /// Set by the waker (from any thread), cleared by the executor when it polls.
    /// Seeded true so a fresh task polls once.
    ready: Arc<AtomicBool>,
}

const VTABLE: RawWakerVTable = RawWakerVTable::new(clone_waker, wake, wake_by_ref, drop_waker);

/// Leak an owned `Arc<AtomicBool>` into a RawWaker (one strong count held by the raw
/// pointer, released by [`wake`] / [`drop_waker`]).
fn raw_waker(ready: Arc<AtomicBool>) -> RawWaker {
    RawWaker::new(Arc::into_raw(ready) as *const (), &VTABLE)
}

/// A Waker that sets ready, holding one strong reference to the flag for its life.
fn waker_for(ready: &Arc<AtomicBool>) -> Waker {
    // SAFETY: raw_waker pairs a leaked `Arc<AtomicBool>` with VTABLE, whose four
    // functions all reconstitute exactly that Arc; `Arc<AtomicBool>` is Send + Sync.
    unsafe { Waker::from_raw(raw_waker(Arc::clone(ready))) }
}

// The four VTABLE slots share one contract, stated here once and referred to by each `# Safety` section
// below: ptr is always a pointer obtained from Arc::into_raw on an `Arc<AtomicBool>` in raw_waker, and
// the strong count it stands for has not been released yet. Each function reconstitutes that Arc and
// either forwards ownership, borrows it without dropping the strong count (ManuallyDrop), or drops it
// exactly once. The atomic store makes a wake from any thread sound.

/// # Safety
/// ptr must satisfy the shared vtable contract above.
unsafe fn clone_waker(ptr: *const ()) -> RawWaker {
    // SAFETY: the shared contract - ptr came from Arc::into_raw on an `Arc<AtomicBool>`. ManuallyDrop
    // borrows it without releasing that strong count; the clone funds the new waker's own.
    let ready = ManuallyDrop::new(unsafe { Arc::from_raw(ptr as *const AtomicBool) });
    raw_waker(Arc::clone(&ready))
}

/// # Safety
/// ptr must satisfy the shared vtable contract above.
unsafe fn wake(ptr: *const ()) {
    // SAFETY: the shared contract - ptr came from Arc::into_raw on an `Arc<AtomicBool>`. This consumes
    // the strong count the waker held, which wake is defined to do.
    let ready = unsafe { Arc::from_raw(ptr as *const AtomicBool) };
    ready.store(true, Ordering::Release);
}

/// # Safety
/// ptr must satisfy the shared vtable contract above.
unsafe fn wake_by_ref(ptr: *const ()) {
    // SAFETY: the shared contract - ptr came from Arc::into_raw on an `Arc<AtomicBool>`. ManuallyDrop
    // borrows it without releasing the strong count, which is what distinguishes this from wake.
    let ready = ManuallyDrop::new(unsafe { Arc::from_raw(ptr as *const AtomicBool) });
    ready.store(true, Ordering::Release);
}

/// # Safety
/// ptr must satisfy the shared vtable contract above.
unsafe fn drop_waker(ptr: *const ()) {
    // SAFETY: the shared contract - ptr came from Arc::into_raw on an `Arc<AtomicBool>`, and this is the
    // one place that strong count is released.
    drop(unsafe { Arc::from_raw(ptr as *const AtomicBool) });
}

/// A single-threaded, frame-ticked local executor. Spawn 'static owning-output
/// futures with [`spawn`](Self::spawn); drive them one poll-per-woken-task per frame
/// with [`tick`](Self::tick). !Send + !Sync (holds an [`Rc`]).
///
/// Never [`tick`](Self::tick) the render path.
pub struct LocalExecutor {
    /// Every live task. A finished or cancelled task (its future gone [`None`]) is
    /// swept out at the end of the [`tick`](Self::tick) that observes it.
    tasks: Rc<RefCell<Vec<Rc<TaskInner>>>>,
}

impl Default for LocalExecutor {
    #[inline]
    fn default() -> Self {
        Self::new()
    }
}

impl LocalExecutor {
    /// An executor with no tasks.
    #[inline]
    pub fn new() -> Self {
        LocalExecutor {
            tasks: Rc::new(RefCell::new(Vec::new())),
        }
    }

    /// Spawn future as a root task and return its owning [`Task`] handle.
    ///
    /// The future must be 'static - it owns its captures and returns its output,
    /// borrowing nothing external. The task is scheduled for its first poll on the next
    /// [`tick`](Self::tick). Read the output off the returned handle with
    /// [`Task::try_take`]; drop the handle to cancel the chain.
    pub fn spawn<F>(&self, future: F) -> Task<F::Output>
    where
        F: Future + 'static,
    {
        let out = Rc::new(RefCell::new(None));
        let out_slot = Rc::clone(&out);
        // Erase the output type: the wrapper drives future and stashes its result in
        // the shared cell, so every stored task is a uniform `Future<Output = ()>`.
        let wrapped: LocalFuture = Box::pin(async move {
            *out_slot.borrow_mut() = Some(future.await);
        });
        let task = Rc::new(TaskInner {
            future: RefCell::new(Some(wrapped)),
            ready: Arc::new(AtomicBool::new(true)),
        });
        self.tasks.borrow_mut().push(Rc::clone(&task));
        Task { inner: task, out }
    }

    /// Poll every task woken since the last tick, once each, then sweep out the
    /// finished and cancelled ones. Returns the number of tasks polled.
    ///
    /// Call once per frame from the main loop. A task that returns
    /// [`Poll::Pending`](core::task::Poll) is polled again only once something wakes it;
    /// a [`HandleFuture`](crate::HandleFuture) wakes itself for the next tick, so a chain
    /// advances at most one pending await per frame, running straight through awaits
    /// that are already ready. A task woken during this tick is polled on the next one.
    pub fn tick(&self) -> usize {
        // Snapshot so a poll can spawn a sibling; tasks spawned now wait for the next tick.
        let snapshot: Vec<Rc<TaskInner>> = self.tasks.borrow().iter().map(Rc::clone).collect();
        let mut polled = 0;
        for task in &snapshot {
            // Acquire pairs with the waker's Release store.
            if !task.ready.swap(false, Ordering::Acquire) {
                continue;
            }
            let Some(mut future) = task.future.borrow_mut().take() else {
                continue; // finished or cancelled since being snapshotted
            };
            polled += 1;
            let waker = waker_for(&task.ready);
            let mut cx = Context::from_waker(&waker);
            if future.as_mut().poll(&mut cx).is_pending() {
                // Restore for the next poll; a task cannot own its own handle, so nothing
                // cancelled it during the poll.
                *task.future.borrow_mut() = Some(future);
            }
        }
        // A task spawned during the polls above still has its future and is retained.
        self.tasks
            .borrow_mut()
            .retain(|task| task.future.borrow().is_some());
        polled
    }
}

/// An owning handle to one spawned root task, carrying the task's output.
///
/// Dropping it cancels the task: the in-flight future is dropped at once, closing its
/// captured [`Handle`](crate::Handle)s without blocking. Read the completed output with
/// [`try_take`](Self::try_take). !Send + !Sync.
pub struct Task<T> {
    inner: Rc<TaskInner>,
    out: Rc<RefCell<Option<T>>>,
}

impl<T> Task<T> {
    /// Take the task's output if it has completed, else [`None`].
    ///
    /// Yields [`Some`] exactly once; a still-running or already-taken task returns [`None`].
    #[inline]
    pub fn try_take(&self) -> Option<T> {
        self.out.borrow_mut().take()
    }

    /// Whether the task's future has finished (completed or been cancelled) and will
    /// not be polled again. The output may still be waiting in [`try_take`](Self::try_take).
    #[inline]
    pub fn is_finished(&self) -> bool {
        self.inner.future.borrow().is_none()
    }
}

impl<T> Drop for Task<T> {
    fn drop(&mut self) {
        // The executor sweeps the emptied task out on its next tick.
        self.inner.future.borrow_mut().take();
    }
}

#[cfg(test)]
mod tests {
    //! The local executor and the Handle future, driven against a fake VFS call table.
    use std::cell::RefCell;
    use std::ptr;

    use core::future::Future;
    use core::pin::Pin;
    use core::task::{Context, Poll as TaskPoll};

    use crate::vfs::fake;
    use crate::{LocalExecutor, Mount, Task};
    use crate::sys;
    use crate::sys::FlError;

    /// One fake async operation: pending for ready_after is_ready checks, then
    /// ready with bytes (or a failure). closed records the fl_vfs_close from the
    /// handle's drop, so both cancellation and normal completion are observable.
    #[derive(Default, Clone)]
    struct Op {
        ready_after: u32,
        bytes: Vec<u8>,
        fail: bool,
        closed: bool,
    }

    #[derive(Default)]
    struct FakeState {
        /// Configs consumed FIFO by read_all_with_options; each launch appends to ops and
        /// returns ops.len() as the ticket (tickets are 1-based, 0 is the sentinel).
        launches: Vec<Op>,
        next_launch: usize,
        ops: Vec<Op>,
    }

    thread_local! {
        static FAKE: RefCell<FakeState> = RefCell::new(FakeState::default());
    }

    /// Reset the fake and queue the ops the test will launch, in launch order.
    fn reset_fake(launches: Vec<Op>) {
        FAKE.with(|f| {
            *f.borrow_mut() = FakeState {
                launches,
                next_launch: 0,
                ops: Vec::new(),
            }
        });
    }

    fn op_closed(ticket: u32) -> bool {
        FAKE.with(|f| f.borrow().ops[ticket as usize - 1].closed)
    }

    fn empty_str() -> sys::RawStr {
        sys::RawStr::EMPTY
    }

    unsafe extern "C" fn fake_read_all_with_options(
        _mount: *mut sys::FlVfsMount,
        _path: sys::RawStr,
        _cb: sys::VfsReadCallback,
        _ud: *mut std::os::raw::c_void,
        _release: sys::VfsReleaseCallback,
        _reuse: u32,
    ) -> u32 {
        FAKE.with(|f| {
            let mut f = f.borrow_mut();
            let op = f.launches[f.next_launch].clone();
            f.next_launch += 1;
            f.ops.push(op);
            f.ops.len() as u32
        })
    }

    unsafe extern "C" fn fake_is_ready(ticket: u32) -> bool {
        FAKE.with(|f| {
            let mut f = f.borrow_mut();
            let op = &mut f.ops[ticket as usize - 1];
            if op.ready_after > 0 {
                op.ready_after -= 1;
                false
            } else {
                true
            }
        })
    }

    unsafe extern "C" fn fake_get_data(ticket: u32) -> sys::VfsData {
        FAKE.with(|f| {
            let f = f.borrow();
            let op = &f.ops[ticket as usize - 1];
            if op.fail {
                sys::VfsData {
                    data: ptr::null_mut(),
                    size: 0,
                    success: false,
                    error_message: empty_str(),
                }
            } else {
                sys::VfsData {
                    data: op.bytes.as_ptr() as *mut u8,
                    size: op.bytes.len() as i64,
                    success: true,
                    error_message: empty_str(),
                }
            }
        })
    }

    unsafe extern "C" fn fake_close(ticket: u32) {
        FAKE.with(|f| f.borrow_mut().ops[ticket as usize - 1].closed = true);
    }

    /// The four calls these tests drive; every other slot panics if reached.
    fn fake_vtable() -> fake::VfsApi {
        fake::VfsApi {
            fl_vfs_mount_read_all_with_options: fake_read_all_with_options,
            fl_vfs_is_ready: fake_is_ready,
            fl_vfs_get_data: fake_get_data,
            fl_vfs_close: fake_close,
            ..fake::VfsApi::unimplemented()
        }
    }

    /// Point this thread's VFS calls at the fake table, and hand back a mount to
    /// drive them through.
    fn null_mount() -> Mount {
        fake::install(fake_vtable());
        // SAFETY: the fake never dereferences the mount pointer it is given.
        unsafe { Mount::from_raw(ptr::null_mut()) }
    }

    /// An op that completes with bytes after ready_after readiness checks.
    fn op(ready_after: u32, bytes: &[u8]) -> Op {
        Op {
            ready_after,
            bytes: bytes.to_vec(),
            ..Default::default()
        }
    }

    /// Tick until task finishes or budget ticks elapse; returns the ticks spent.
    /// The budget stops a never-completing task from hanging the test.
    fn drive<T>(exec: &LocalExecutor, task: &Task<T>, budget: usize) -> usize {
        for ticks in 1..=budget {
            exec.tick();
            if task.is_finished() {
                return ticks;
            }
        }
        panic!("task did not finish within {budget} ticks");
    }

    /// A `Handle<VfsData>`-backed leaf future completes through the executor: the async
    /// task suspends on the pending handle, self-wakes each tick, and yields the result
    /// once the handle is ready.
    #[test]
    fn handle_leaf_completes() {
        reset_fake(vec![op(2, b"42")]);
        let mount = null_mount();
        let handle = mount.read("foo.txt").expect("launch");

        let exec = LocalExecutor::new();
        let task = exec.spawn(async move { handle.await });

        // The leaf is pending for two polls, so it takes three ticks (one poll per tick).
        let ticks = drive(&exec, &task, 10);
        assert_eq!(ticks, 3, "one poll per tick until the handle is ready");
        match task.try_take() {
            Some(Ok(data)) => assert_eq!(data.as_slice(), b"42"),
            other => panic!("expected ready-ok, got {other:?}"),
        }
        assert!(task.try_take().is_none(), "output is yielded exactly once");
        assert!(op_closed(1), "completed leaf's ticket was closed");
    }

    /// An immediately-ready leaf completes on the first tick - an already-ready await
    /// does not suspend the task.
    #[test]
    fn immediately_ready_leaf_completes_in_one_tick() {
        reset_fake(vec![op(0, b"done")]);
        let mount = null_mount();
        let handle = mount.read("foo.txt").expect("launch");

        let exec = LocalExecutor::new();
        let task = exec.spawn(async move { handle.await });

        assert_eq!(drive(&exec, &task, 4), 1);
        match task.try_take() {
            Some(Ok(data)) => assert_eq!(data.as_slice(), b"done"),
            other => panic!("expected ready-ok, got {other:?}"),
        }
    }

    /// A genuinely sequential chain - step 2's operation is issued only after step 1
    /// completes - runs straight through on the executor, threading step 1's value into
    /// step 2.
    #[test]
    fn sequential_chain_completes() {
        reset_fake(vec![op(1, b"first"), op(1, b"second")]);
        let mount = null_mount();

        let exec = LocalExecutor::new();
        let task = exec.spawn(async move {
            let first = mount.read("a.txt").expect("launch").await?;
            let second = mount.read("b.txt").expect("launch").await?;
            Ok::<usize, FlError>(first.len() + second.len())
        });

        drive(&exec, &task, 12);
        assert_eq!(task.try_take(), Some(Ok(b"first".len() + b"second".len())));
    }

    /// The operation's error reaches the caller through the Result channel unchanged,
    /// and the chain short-circuits at the failing step (via ?).
    #[test]
    fn leaf_error_propagates() {
        reset_fake(vec![Op {
            ready_after: 1,
            fail: true,
            ..Default::default()
        }]);
        let mount = null_mount();

        let reached_tail = std::rc::Rc::new(std::cell::Cell::new(false));
        let reached_flag = std::rc::Rc::clone(&reached_tail);

        let exec = LocalExecutor::new();
        let task = exec.spawn(async move {
            let data = mount.read("bad.txt").expect("launch").await?;
            reached_flag.set(true);
            Ok::<usize, FlError>(data.len())
        });

        drive(&exec, &task, 6);
        assert_eq!(task.try_take(), Some(Err(FlError::GenericError)));
        assert!(
            !reached_tail.get(),
            "chain short-circuited at the failing step"
        );
    }

    /// Dropping a root task cancels its chain: the in-flight leaf's ticket is closed
    /// (the non-blocking fl_vfs_close) without polling to completion, the tail after the
    /// await never runs, and a later tick does no work.
    #[test]
    fn drop_root_task_cancels_chain() {
        // ready_after = u32::MAX: the leaf never completes.
        reset_fake(vec![op(u32::MAX, b"")]);
        let mount = null_mount();
        let handle = mount.read("slow.txt").expect("launch");

        let completed = std::rc::Rc::new(std::cell::Cell::new(false));
        let completed_flag = std::rc::Rc::clone(&completed);

        let exec = LocalExecutor::new();
        let task = exec.spawn(async move {
            let _ = handle.await;
            completed_flag.set(true);
        });

        // One tick suspends the task on the pending leaf (self-woken for the next tick).
        assert_eq!(exec.tick(), 1);
        assert!(!op_closed(1), "leaf still in flight");
        assert!(!task.is_finished());

        drop(task);
        assert!(op_closed(1), "leaf ticket closed without join-spin");

        // The cancelled task self-woke before the drop, so it is still enqueued - the next
        // tick finds it emptied and skips it. The chain never completed.
        assert_eq!(exec.tick(), 0, "cancelled task is not polled");
        assert!(!completed.get(), "chain body past the await never ran");
    }

    /// Several tasks on one executor make progress independently in the same tick: each
    /// is polled while woken and completes on its own schedule, and the cancelled one
    /// stops without disturbing the others.
    #[test]
    fn multiple_tasks_progress_independently() {
        reset_fake(vec![op(0, b"fast"), op(2, b"slow"), op(u32::MAX, b"")]);
        let mount = null_mount();

        let fast_handle = mount.read("fast.txt").expect("launch");
        let slow_handle = mount.read("slow.txt").expect("launch");
        // A third task that never completes; we cancel it mid-flight.
        let doomed_handle = mount.read("doomed.txt").expect("launch");

        let exec = LocalExecutor::new();
        let fast = exec.spawn(async move { fast_handle.await });
        let slow = exec.spawn(async move { slow_handle.await });
        let doomed = exec.spawn(async move { doomed_handle.await });

        assert_eq!(exec.tick(), 3);
        assert!(fast.is_finished());
        match fast.try_take() {
            Some(Ok(data)) => assert_eq!(data.as_slice(), b"fast"),
            other => panic!("expected ready-ok, got {other:?}"),
        }
        assert!(!slow.is_finished());

        // Cancel the doomed task: the others must be unaffected.
        drop(doomed);
        assert!(op_closed(3), "cancelled task's leaf ticket closed");

        // The slow task keeps going; only it is still woken (fast is finished, doomed
        // cancelled), so later ticks poll exactly one task.
        assert_eq!(exec.tick(), 1);
        assert_eq!(exec.tick(), 1);
        assert!(slow.is_finished());
        match slow.try_take() {
            Some(Ok(data)) => assert_eq!(data.as_slice(), b"slow"),
            other => panic!("expected ready-ok, got {other:?}"),
        }
    }

    /// A task that spawns a sibling mid-poll lands the child after the current tick's
    /// snapshot: the child is not polled within the spawning frame, but runs and completes
    /// on later ticks.
    #[test]
    fn task_spawned_during_poll_runs_next_tick() {
        // The child's op is immediately ready: if the executor wrongly polled the child
        // within the spawning tick, its flag would flip that frame.
        reset_fake(vec![op(0, b"child")]);
        let mount = null_mount();
        let handle = mount.read("child.txt").expect("launch");

        // The executor is shared into the parent future (owned Rc, 'static) so the poll
        // can re-enter spawn.
        let exec = std::rc::Rc::new(LocalExecutor::new());
        let child_done = std::rc::Rc::new(std::cell::Cell::new(false));
        // Park the child's Task handle here so it isn't dropped - and thereby cancelled -
        // when the parent future completes.
        let child_task: std::rc::Rc<RefCell<Option<Task<()>>>> =
            std::rc::Rc::new(RefCell::new(None));

        let exec_for_parent = std::rc::Rc::clone(&exec);
        let child_done_flag = std::rc::Rc::clone(&child_done);
        let child_slot = std::rc::Rc::clone(&child_task);
        let parent = exec.spawn(async move {
            let child = exec_for_parent.spawn(async move {
                let _ = handle.await;
                child_done_flag.set(true);
            });
            *child_slot.borrow_mut() = Some(child);
        });

        assert_eq!(
            exec.tick(),
            1,
            "only the parent is polled on the spawning tick"
        );
        assert!(parent.is_finished());
        assert!(
            !child_done.get(),
            "child not polled within the spawning frame (its op is ready, so a poll would flip this)"
        );

        for _ in 0..5 {
            if child_done.get() {
                break;
            }
            exec.tick();
        }
        assert!(
            child_done.get(),
            "spawned-during-poll child ran on a later tick"
        );
    }

    /// A poll that crosses several already-ready awaits runs straight through them in a
    /// single tick - a chain advances by whole ready-runs, and only a pending await costs
    /// a frame. Complements the single-await ready case with a two-in-a-row run-through.
    #[test]
    fn ready_awaits_run_through_in_one_poll() {
        reset_fake(vec![op(0, b"a"), op(0, b"bb")]);
        let mount = null_mount();

        let exec = LocalExecutor::new();
        let task = exec.spawn(async move {
            let a = mount.read("a.txt").expect("launch").await?;
            // Both leaves are immediately ready, so this second await resolves in the same
            // poll as the first - no frame is spent between them.
            let b = mount.read("b.txt").expect("launch").await?;
            Ok::<usize, FlError>(a.len() + b.len())
        });

        assert_eq!(
            drive(&exec, &task, 4),
            1,
            "a ready-run completes in one tick"
        );
        assert_eq!(task.try_take(), Some(Ok(b"a".len() + b"bb".len())));
    }

    /// A future that returns Pending without waking is polled once and never again -
    /// proof the executor is genuinely waker-driven, so a leaf that forgot to self-wake
    /// would stall (and, conversely, self-waking is load-bearing).
    #[test]
    fn pending_without_wake_is_not_repolled() {
        let polls = std::rc::Rc::new(std::cell::Cell::new(0));
        let fut = CountingPending {
            polls: std::rc::Rc::clone(&polls),
            wake_each_poll: false,
        };

        let exec = LocalExecutor::new();
        let _task = exec.spawn(fut);

        for _ in 0..5 {
            exec.tick();
        }
        assert_eq!(
            polls.get(),
            1,
            "an un-woken pending task is polled exactly once"
        );
    }

    /// A self-waking pending future is re-polled on every tick - the mechanism the
    /// [`HandleFuture`] leaf relies on to make progress across frames.
    #[test]
    fn self_waking_task_is_repolled_each_tick() {
        let polls = std::rc::Rc::new(std::cell::Cell::new(0));
        let fut = CountingPending {
            polls: std::rc::Rc::clone(&polls),
            wake_each_poll: true,
        };

        let exec = LocalExecutor::new();
        let _task = exec.spawn(fut);

        for _ in 0..4 {
            exec.tick();
        }
        assert_eq!(polls.get(), 4, "a self-waking task is re-polled every tick");
    }

    /// A future that counts its polls and stays Pending forever, optionally self-waking
    /// each poll - the probe for the executor's waker discipline.
    struct CountingPending {
        polls: std::rc::Rc<std::cell::Cell<u32>>,
        wake_each_poll: bool,
    }

    impl Future for CountingPending {
        type Output = ();

        fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> TaskPoll<()> {
            self.polls.set(self.polls.get() + 1);
            if self.wake_each_poll {
                cx.waker().wake_by_ref();
            }
            TaskPoll::Pending
        }
    }

    /// A wake delivered from another OS thread re-polls the task on the executor's own
    /// thread - the runtime proof that the executor's [`Waker`] genuinely honours its
    /// Send + Sync contract (its backing store is an `Arc<AtomicBool>`, safe to wake
    /// cross-thread). The leaf never self-wakes, so the only thing that can re-poll it
    /// is the worker thread's wake().
    #[test]
    fn cross_thread_wake_repolls_task() {
        use std::sync::atomic::{AtomicBool, Ordering};
        use std::sync::Arc;

        // Set by the worker thread just before it wakes us, so the second poll can confirm
        // the wake really came from off-thread.
        let woken_from_thread = Arc::new(AtomicBool::new(false));

        let exec = LocalExecutor::new();
        let task = exec.spawn(CrossThreadWake {
            started: false,
            flag: Arc::clone(&woken_from_thread),
            worker: None,
        });

        // First tick polls once: the leaf hands a cloned Waker to a worker thread and
        // returns Pending without self-waking. It cannot progress on its own.
        assert_eq!(exec.tick(), 1);
        assert!(!task.is_finished());

        // Drive until the cross-thread wake lands. Bounded so a lost wake fails loudly
        // instead of hanging; a task that isn't ready simply isn't polled (tick -> 0).
        for _ in 0..2000 {
            if task.is_finished() {
                break;
            }
            exec.tick();
            std::thread::sleep(std::time::Duration::from_millis(1));
        }
        assert!(
            task.is_finished(),
            "the cross-thread wake re-polled the task"
        );
        assert!(
            woken_from_thread.load(Ordering::Acquire),
            "completion followed the worker thread's wake"
        );
    }

    /// A leaf that, on its first poll, moves a cloned [`Waker`] to a worker thread which
    /// wakes it after a beat, then completes on the wake-driven second poll. Holds the
    /// worker's [`JoinHandle`]; it is never joined - dropping it detaches the
    /// already-finished thread, which is fine for a throwaway test leaf.
    struct CrossThreadWake {
        started: bool,
        flag: std::sync::Arc<std::sync::atomic::AtomicBool>,
        worker: Option<std::thread::JoinHandle<()>>,
    }

    impl Future for CrossThreadWake {
        type Output = ();

        fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> TaskPoll<()> {
            use std::sync::atomic::Ordering;
            if !self.started {
                self.started = true;
                // Waker: Send is what makes this move legal - the whole point under test.
                let waker = cx.waker().clone();
                let flag = std::sync::Arc::clone(&self.flag);
                self.worker = Some(std::thread::spawn(move || {
                    std::thread::sleep(std::time::Duration::from_millis(5));
                    flag.store(true, Ordering::Release);
                    waker.wake();
                }));
                return TaskPoll::Pending;
            }
            // Reached only because the worker thread woke us (the leaf never self-wakes).
            TaskPoll::Ready(())
        }
    }
}
