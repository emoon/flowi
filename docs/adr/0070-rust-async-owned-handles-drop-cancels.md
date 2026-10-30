# The Rust async surface: owning handles, drop-cancellation, one frame-ticked executor

Status: accepted

## Context

The C VFS is non-blocking and ticket-based: an async launch returns a recycled `u32`
(`FlVfsHandle`), `vfs_is_ready(ticket)` is the completion check, `vfs_get_*` drains the
result, and `vfs_close` releases the ticket. Tickets are recycled aggressively — the
toolkit hands out id 4, the caller closes it, and id 4 is immediately reused for an
unrelated operation. A failed launch reports itself in band, as
`FL_VFS_HANDLE_INVALID`.

Exposing that shape directly in Rust reproduces its two hazards: a stale `u32` is
indistinguishable from a live one, and an in-band sentinel is easy to forget to check.
The crate also needs a second face on the same C calls — the frame loop wants to poll,
a job body wants to block — and it needs a way to run multi-step chains (read, then
parse, then read again) that stay cooperative with the frame loop, without asyncifying
the render path. There is no legal await point in an immediate-mode draw walk.

## Decision

**A ticket is represented by an owning, non-`Copy`, non-`Clone` `Handle<T>`, and every
async surface in the crate is built from that one primitive.**

- **Ownership is the ABA guard.** `Handle<T>` is a `u32` ticket plus a done flag on the
  stack, with a zero-sized payload marker and no allocation. Because it cannot be copied
  or cloned and closes its ticket exactly when it drops, using a stale recycled ticket is
  unrepresentable rather than merely discouraged.
- **`Option<Handle<T>>` replaces the sentinel.** A failed launch is `None`. A live
  `Handle` therefore always holds a real ticket, and no caller-side sentinel comparison
  exists to forget.
- **Drop is a non-blocking close, never a join-spin.** Dropping a `Handle` — completed or
  still in flight — calls `vfs_close`, which returns immediately; the toolkit defers the
  real free itself (see ADR-0074). Drop never loops polling an operation to completion. The
  operation itself is not aborted: what drop releases is the ticket, and the I/O runs to
  completion on the worker before the VFS reclaims it.
- **Callback-owned storage is lent to the C side, which hands it back.** A `read_with`
  transform lives in a boxed slot passed as the read's `user_data`, together with a release
  hook (`FlVfsReleaseCallback`) the VFS invokes exactly once when it frees the handle - after
  the job, after the read callback, and never before the handle is closed. The hook is where
  the slot is dropped, so a `MappedRead` dropped mid-flight never frees storage the worker can
  still reach, and the Rust side keeps no reference count of its own. The same hook is what
  a C caller with heap `user_data` uses, since a deferred close gives it no other signal.
- **Cancellation is drop.** Dropping a `Task` drops the in-flight future now, which drops
  its captured leaf `Handle`s and closes their tickets. Because a root future owns its
  buffers and never partially mutates shared state, a mid-flight cancel leaves nothing
  half-written, so cancellation needs no unwind protocol of its own.
- **One executor: single-threaded, `!Send`, ticked once per frame.** `LocalExecutor::tick`
  is called by the host once per frame from its own update step; the runtime does not tick it. Tasks
  resume on the thread that owns the UI context and honour the per-frame arena rewind — a suspended task's persistent
  state lives in its own owned captures, never in a `&frame_arena` borrow held across an
  await. Root futures are `Future + 'static` and return their result. The executor and its
  tasks hold `Rc`, so the whole surface is `!Send + !Sync` by construction, matching
  `Handle`, whose payload marker is a raw pointer.
- **Leaf futures self-wake; a waker names only an `AtomicBool`.** The underlying async C
  operations complete with no path back to the executor, so the `Handle<T>` → `Future`
  bridge re-arms itself on every `Poll::Pending` by waking its own waker. `Waker` is
  unconditionally `Send + Sync`, so safe code inside any spawned future may clone one and
  move it to another thread; the waker must therefore be sound to touch from any thread.
  It names only its task's `Arc<AtomicBool>` readiness flag — waking is one atomic store,
  and that flag is the only cross-thread-reachable state. The `!Send` future and the
  `Rc`-based task registry are never touched off the executor's thread.

`WorkerMount` is the reviewed way to reach the same C calls from a job body: a `Send` view
carrying blocking counterparts that return the finished value, so a worker thread never
holds a `Handle`.

## Considered Options

- **Expose the raw `u32` ticket with a `close()` method.** Rejected: it keeps both hazards
  the C API has, and makes every recycled-id bug a runtime mystery rather than a compile
  error.
- **`Clone`-able handles with reference counting.** Rejected: the C ticket has exactly one
  owner and one close; refcounting invents a second lifetime model on top and would make
  "who closes this" a runtime question.
- **Drop joins the operation to completion.** Rejected: drop then blocks the frame loop for
  an arbitrary I/O wait, and cancellation stops being free.
- **A multi-threaded executor, or an off-the-shelf one (tokio, async-std).** Rejected: the
  UI context, the frame arena, and the handles are all thread-affine, so a work-stealing
  executor could not run these futures anyway; and a runtime dependency for a
  poll-once-per-frame loop is far more machinery than the shape needs.

## Consequences

- A `Handle` cannot be stored in a `Send` container, moved to a worker thread, or held
  across a frame-arena rewind in borrowed form. Job bodies use `WorkerMount` instead.
- Forgetting to poll a handle leaks nothing worse than a ticket held open until drop.
- A future that wants to survive cancellation cleanly must own its buffers; one that
  mutates shared state mid-await would break the "nothing half-written" property, and that
  is a review concern rather than a compile-time one.
- Because the waker only stores to an `AtomicBool`, a task woken from another thread is
  not polled until the next `tick`. Latency is one frame, which is the intended granularity.
