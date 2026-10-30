# Rust closures cross the C job boundary as a boxed trampoline that owns its own free

Status: accepted

## Context

`fl_jobs_*` is flowi core's work-stealing job system: a scheduled job is a C function
pointer plus a `void*` user-data pointer, dispatched on a worker thread at an unknown
later time. `Jobs::add` lets Rust code schedule a closure against it.

Two things do not cross that boundary on their own. A Rust closure is not a C function
pointer, so it has to be boxed and reached through an `extern "C"` trampoline. And a Rust
panic must not unwind across the C frame that invoked it — the job body runs inside a C
worker loop, so an escaping unwind is undefined behaviour.

Freeing the box is the part with a real choice in it. flowi core invokes each job function
exactly once, and it never frees a user-data pointer that did not come from its own context
pool — a Rust `Box` never does. So the C side will not free it, and the Rust side has no
later point at which it can: `Jobs::add` returns before the job runs, and `JobHandle` is a
plain id that carries no destructor.

## Decision

**The trampoline owns the free.** It reconstructs the `Box` from the `void*`, runs the
closure exactly once, and drops the box when the run returns. Nothing else frees it.

- **The closure is `Send + 'static`.** It runs on a worker thread at an unknown later time,
  so it may not borrow from the call site. This falls out of the boundary rather than being
  imposed on top of it.
- **A panic inside the closure is caught at the trampoline and logged at error level.** The
  job fails to finish its work; the process is not aborted. Containment is mandatory
  (unwinding into C is UB); aborting on top of it is not, and a failed background job is
  recoverable in a way a dead process is not.
- **A job scheduled but never dispatched leaks its one box.** This happens only when the
  job system is destroyed with work still queued, at which point the process is on its way
  down. The leak is memory only and never unsafe.
- **Scheduling entry points require the foundation to already be running** and panic if it
  is not, rather than dereferencing C's null global. Bringing the job system up and tearing
  it down is the runtime's responsibility (`Application::new`, `Application::embedded`, or a
  bare `flowi::init`), not part of this safe surface.

## Considered Options

- **Free the box from a completion callback.** Rejected: the job system has no completion
  callback, and adding one to C to solve a Rust-side ownership question is the wrong seam.
- **Keep a registry of live boxes and reap it from the frame loop.** Rejected: it invents a
  second lifetime model, needs synchronisation with the worker threads, and buys only the
  destroyed-with-work-queued leak — a shutdown-path leak of one allocation.
- **Abort the process on a caught panic.** Rejected: the alternative is not "silently
  wrong", it is "this job did not finish, and it said so in the log". Turning every panicking
  background job into a process kill is a worse default for a UI toolkit.
- **Require `UnwindSafe` closures instead of asserting it.** Rejected: `UnwindSafe` would
  reject ordinary captures for a property the boundary already guarantees another way — the
  box is dropped either way, and no state is observable after the catch except through the
  poison flag.

## Consequences

- A closure cannot borrow from the call site; captured data must be owned or `Arc`-shared.
  This is the same constraint any thread-spawning API imposes.
- A panicking job is visible only in the log. Code that needs to know a job failed must
  report that itself through its own captured channel.
- The trampoline is the only place that frees the box, so it must run exactly once. That is
  guaranteed by flowi core's dispatch contract, and it is the assumption to re-check if the
  job system ever gains a cancel-before-dispatch path.
