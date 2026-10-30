//! Safe Rust over flowi-core: the C foundation the toolkit and its plugins are built on.
//!
//! Core is the process-wide foundation - an arena allocator, a job system, logging, path
//! and string handling, a file watcher - and nothing above it. It owns no window, no
//! renderer and no filesystem abstraction.
//!
//! Everything here is a borrow-checked wrapper over the private `sys` module, which holds the raw
//! ABI and all the `unsafe`. The one process-wide obligation is the foundation itself:
//! [`init`] brings it up and [`shutdown`] takes it down, and every surface that needs it
//! brings it up for itself, so a caller reaching core only through [`Arena`] or [`Jobs`]
//! never calls either.

use std::sync::atomic::{AtomicPtr, Ordering};
use std::sync::Mutex;

use crate::sys;






/// The typed, borrow-checked arena: [`Arena::alloc`] returns a `&mut T` tied to the borrow,
/// so [`Arena::rewind`] is a borrow error while an allocation is live. See the module docs.
mod arena;
pub use arena::{Arena, ArenaString, ArenaVec, BaseArena};

/// The job system: a closure-taking façade over the C worker pool, with priorities and
/// dependencies between queued jobs.
mod jobs;
pub use jobs::{JobHandle, JobPriority, Jobs, JobsResult, WorkerInfo};

/// Path manipulation - join, parent, absoluteness - with owned `String` results.
pub mod path;

/// Native file-system change notification: start a watch, drain the batch it accumulated.
mod file_watcher;
pub use file_watcher::{FileChange, FileChanges, FileWatcher, WatchConfig};

/// The bridge from the `log` crate to core's channel logging, and the log-file control
/// surface. Installed automatically when the foundation comes up.
mod log_bridge;
pub use log_bridge::install_log_bridge;
pub use log_bridge::set_target_level;
pub use log_bridge::LogFile;

/// Raw C ABI item; prefer the safe wrappers. Wrap the body of an extern "C" entry point so a Rust
/// panic is caught instead of unwinding across the C ABI.
pub use crate::sys::ffi_guard;
/// Raw C ABI item; prefer the safe wrappers. Install the logger [`ffi_guard`] hands a caught panic's
/// payload to.
pub use crate::sys::set_panic_logger;
/// Raw C ABI item; prefer the safe wrappers. Clear the poison [`ffi_guard`] sets and the installed
/// panic logger, for a host that reloads the plugin.
pub use crate::sys::clear_poison;
/// Raw C ABI item; prefer the safe [`Arena`]. The opaque C FlArena behind `*mut FlArena`.
pub use crate::sys::Arena as FlArena;
/// Raw C ABI item; prefer the safe [`Arena`]. Create a named arena.
pub use crate::sys::fl_arena_create;
/// Raw C ABI item; prefer the safe [`Arena`]. Destroy an arena from [`fl_arena_create`].
pub use crate::sys::fl_arena_destroy;
/// Raw C ABI item; prefer the safe [`Jobs`]. Queue a C job function with an opaque payload.
pub use crate::sys::fl_jobs_add_job;
/// Raw C ABI item; prefer the safe [`Jobs`]. Queue a C job that runs once its dependency finishes.
pub use crate::sys::fl_jobs_add_job_with_dependency;
/// Raw C ABI item; prefer the safe [`Jobs`]. Whether a queued job has finished.
pub use crate::sys::fl_jobs_is_finished;
/// Raw C ABI item; prefer the safe [`Jobs`]. Block until a queued job finishes.
pub use crate::sys::fl_jobs_wait;
/// Raw C ABI item; prefer the safe [`WorkerInfo`]. The worker identity a job body is handed.
pub use crate::sys::JobsWorkerInfo;
/// Raw C ABI item; prefer the safe profiler scope guards. Open a named profiling scope.
pub use crate::sys::fl_perf_scope_begin;
/// Raw C ABI item; prefer the safe profiler scope guards. Close the scope [`fl_perf_scope_begin`] opened.
pub use crate::sys::fl_perf_scope_end;
/// Raw C ABI item; prefer the safe profiler scope guards. Open the per-frame profiling scope.
pub use crate::sys::fl_perf_scope_frame_begin;
/// Raw C ABI item; prefer the safe profiler scope guards. Close the per-frame profiling scope.
pub use crate::sys::fl_perf_scope_frame_end;
/// Raw C ABI item; prefer the safe [`Arena`]. Allocate count uninitialised T out of a raw arena.
pub use crate::sys::arena_alloc_array;
/// Raw C ABI item; prefer the safe [`Arena`]. Move one T into a raw arena.
pub use crate::sys::arena_box;
/// Raw C ABI item; view a C (pointer, count) pair as a slice, empty for null or zero count.
pub use crate::sys::borrow_slice;
/// Raw C ABI item; the mutable form of [`borrow_slice`].
pub use crate::sys::borrow_slice_mut;

/// The one live main arena `fl_init` returned, or null when the foundation is down.
/// Doubles as the "is it up?" flag; written only under [`BASE_LOCK`], and read lock-free
/// only by [`is_up`].
static BASE_ARENA: AtomicPtr<sys::Arena> = AtomicPtr::new(core::ptr::null_mut());
static BASE_LOCK: Mutex<()> = Mutex::new(());

/// Bring the foundation up once, serialised under [`BASE_LOCK`]; later callers reuse the live
/// arena and their `job_threads` is ignored.
///
/// Invariant: [`BASE_ARENA`] is only written, and `fl_init` / `fl_destroy` only run, while
/// [`BASE_LOCK`] is held.
fn init_raw(job_threads: i32) -> *mut sys::Arena {
    // Installed first so records emitted during bring-up are captured.
    install_log_bridge();

    let _guard = BASE_LOCK.lock().unwrap_or_else(|e| e.into_inner());

    let live = BASE_ARENA.load(Ordering::Acquire);
    if !live.is_null() {
        return live;
    }

    // SAFETY: the one-time foundation bring-up, serialised by BASE_LOCK.
    let raw = unsafe { sys::fl_init(job_threads) };
    BASE_ARENA.store(raw, Ordering::Release);
    raw
}

/// Whether the foundation is up, as a single atomic load - no lock, no bring-up.
#[inline]
pub fn is_up() -> bool {
    !BASE_ARENA.load(Ordering::Acquire).is_null()
}

/// Bring the process-wide foundation up with `job_threads` background workers, and borrow
/// the main arena it created.
///
/// Every core entry point does this for itself, so a caller reaching core only through
/// [`Arena`] or [`Jobs`] never calls it. It is for the one that has to touch the foundation
/// first - a test harness, or an embedder placing its own allocations in the main arena.
///
/// Idempotent: a foundation that is already up is adopted and `job_threads` ignored. The
/// returned view only allocates and never destroys the arena - that is [`shutdown`]'s job -
/// so any number of views can coexist without one rewinding another's allocations.
///
/// # Panics
/// Panics if the main arena could not be created (the process is out of address space).
pub fn init(job_threads: i32) -> BaseArena {
    let raw = init_raw(job_threads);
    assert!(
        !raw.is_null(),
        "flowi_core: fl_init failed to create the main arena"
    );
    // SAFETY: non-null, and the live main arena: init_raw returns the arena of the foundation
    // that is up, which stays up until the unsafe shutdown, whose contract forbids running it
    // while any view is alive.
    unsafe { BaseArena::new(raw) }
}

/// Tear the process-wide foundation down: the job system, the logging and perf tiers, the
/// main arena every long-lived allocation came from, and the calling thread's scratch
/// arenas.
///
/// Only for a host that wants a deterministic shutdown - a leak checker at process exit, or
/// a library embedding core that has to leave the process clean. A later constructor brings
/// the foundation back up, and calling this twice is a no-op.
///
/// # Safety
/// Nothing may use the foundation during or after the call, until a constructor brings it
/// back up:
/// - every [`BaseArena`] and every allocation made through one is dead;
/// - no surface built on core (an application, a [`Jobs`] job, a [`FileWatcher`]) is alive,
///   and no other thread is inside a core call;
/// - no scratch scope is open on the calling thread.
pub unsafe fn shutdown() {
    let _guard = BASE_LOCK.lock().unwrap_or_else(|e| e.into_inner());

    let was_up = BASE_ARENA.swap(core::ptr::null_mut(), Ordering::AcqRel);
    if !was_up.is_null() {
        // SAFETY: the foundation was up, the swap means nothing else will destroy it, and the
        // caller guarantees nothing is using it.
        unsafe { sys::fl_destroy() };
    }
}
