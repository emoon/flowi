//! Thread-local allocation assertions for tests using Flowi's global allocator.
//! Enabled only with the `allocation-probe` feature.

use std::cell::Cell;

thread_local! {
    /// Whether this thread is inside [`assert_no_alloc`].
    static ARMED: Cell<bool> = const { Cell::new(false) };
    /// Allocations this thread made while armed.
    static ALLOCATIONS: Cell<usize> = const { Cell::new(0) };
}

/// Count one allocation if this thread is armed. Silent during TLS teardown.
pub(crate) fn record_allocation() {
    if ARMED.try_with(Cell::get).unwrap_or(false) {
        let _ = ALLOCATIONS.try_with(|count| count.set(count.get().saturating_add(1)));
    }
}

/// Run `body` with allocation counting armed for this thread, and panic if it
/// allocated.
///
/// `what` names the path under test and appears in the failure message. `body`
/// must not spawn threads or hand work to a pool — only the calling thread is
/// observed.
///
/// # Panics
/// Panics if `body` allocated, or if called from inside another `assert_no_alloc`.
pub fn assert_no_alloc<T>(what: &str, body: impl FnOnce() -> T) -> T {
    assert!(!ARMED.get(), "allocation probes must not be nested");
    ALLOCATIONS.set(0);
    struct Disarm;
    impl Drop for Disarm {
        fn drop(&mut self) {
            ARMED.set(false);
        }
    }
    ARMED.set(true);
    let guard = Disarm;
    let result = body();
    drop(guard);
    let count = ALLOCATIONS.get();

    assert_eq!(count, 0, "{what} allocated {count} time(s); it must not");
    result
}

#[cfg(all(test, feature = "global-allocator"))]
mod tests {
    use super::*;

    #[test]
    fn probe_observes_an_allocation() {
        ALLOCATIONS.set(0);
        ARMED.set(true);
        let allocated = std::hint::black_box(Vec::<u8>::with_capacity(1024));
        ARMED.set(false);
        let count = ALLOCATIONS.get();

        drop(allocated);
        assert!(count > 0, "the probe saw no allocation for a fresh Vec");
    }

    #[test]
    fn probe_passes_on_an_allocation_free_body() {
        let mut scratch = vec![0.0f32; 64];
        assert_no_alloc("filling a preallocated buffer", || {
            scratch.fill(1.0);
        });
        assert_eq!(scratch[0], 1.0);
    }

    // Spawning and joining stay outside the armed window: both allocate on this thread.
    #[test]
    fn probe_ignores_other_threads() {
        use std::sync::atomic::{AtomicBool, Ordering};
        use std::sync::Arc;

        let stop = Arc::new(AtomicBool::new(false));
        let noisy = {
            let stop = Arc::clone(&stop);
            std::thread::spawn(move || {
                let mut churn = 0usize;
                while !stop.load(Ordering::Relaxed) {
                    churn += String::from("allocate me").len();
                }
                churn
            })
        };

        let mut scratch = vec![0.0f32; 64];
        ALLOCATIONS.set(0);
        ARMED.set(true);
        scratch.fill(1.0);
        ARMED.set(false);
        let count = ALLOCATIONS.get();

        stop.store(true, Ordering::Relaxed);
        if noisy.join().is_err() {
            panic!("the noisy thread panicked");
        }
        assert_eq!(count, 0, "another thread's allocations were counted");
    }
}
