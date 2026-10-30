//! Panic containment at the C→Rust boundary.
//!
//! Every extern "C" entry a plugin exports wraps its body in [`ffi_guard`]: a
//! Rust panic must never unwind across the C ABI. On a caught panic the guard logs
//! the payload through the installed panic logger (see [`set_panic_logger`]),
//! poisons the plugin, and returns the caller-supplied error value for this slot.
//!
//! Poison is process-global and terminal for the plugin's lifetime: every subsequent
//! guarded entry early-returns its error slot without running its body. A host that
//! reloads the plugin clears it, and the panic logger with it, via [`clear_poison`];
//! the host installs a fresh logger for the reloaded plugin.
//!
//! A plugin cdylib must be built with the unwind panic strategy (the workspace
//! default) so [`ffi_guard`]'s catch_unwind can contain the panic.

use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};

/// Set once the plugin has caught a panic; guarded entries then early-return.
static POISONED: AtomicBool = AtomicBool::new(false);

/// The installed panic logger as a fn(&str) stored as its address (0 = none).
static PANIC_LOGGER: AtomicUsize = AtomicUsize::new(0);

/// Install the sink [`ffi_guard`] logs a panic payload through. Absent an
/// installed logger the payload is dropped - the guard still poisons and returns
/// the error.
pub fn set_panic_logger(logger: fn(&str)) {
    PANIC_LOGGER.store(logger as usize, Ordering::Release);
}

fn log_panic(message: &str) {
    let addr = PANIC_LOGGER.load(Ordering::Acquire);
    if addr != 0 {
        // SAFETY: addr is only ever set by set_panic_logger from a live
        // fn(&str); a non-zero value is that exact pointer.
        let logger: fn(&str) = unsafe { std::mem::transmute::<usize, fn(&str)>(addr) };
        logger(message);
    }
}

/// Whether the plugin has been poisoned by a prior caught panic.
#[inline]
pub fn is_poisoned() -> bool {
    POISONED.load(Ordering::Acquire)
}

/// Mark the plugin poisoned. Exposed for a host that detects an unrecoverable
/// state out of band; [`ffi_guard`] calls it on every caught panic.
#[inline]
pub fn poison() {
    POISONED.store(true, Ordering::Release);
}

/// Clear the poison flag and the installed panic logger, for a host re-initialising a
/// freshly reloaded plugin. A later panic logs nothing until [`set_panic_logger`] installs
/// a new logger.
#[inline]
pub fn clear_poison() {
    POISONED.store(false, Ordering::Release);
    PANIC_LOGGER.store(0, Ordering::Release);
}

/// Extract a printable message from a panic payload without ever panicking.
pub fn payload_message(payload: &(dyn std::any::Any + Send)) -> &str {
    if let Some(s) = payload.downcast_ref::<&'static str>() {
        s
    } else if let Some(s) = payload.downcast_ref::<String>() {
        s.as_str()
    } else {
        "<non-string panic payload>"
    }
}

/// Run body under panic containment, returning err if the plugin is already
/// poisoned or if body panics.
///
/// Wrap the body of every extern "C" plugin entry in this, passing the C error
/// value the slot returns on failure (e.g. a status code, a null handle, false).
pub fn ffi_guard<T, F: FnOnce() -> T>(err: T, body: F) -> T {
    if is_poisoned() {
        return err;
    }
    // AssertUnwindSafe: a caught panic poisons the plugin, so no guarded body observes
    // the partially-updated state afterwards.
    match catch_unwind(AssertUnwindSafe(body)) {
        Ok(value) => value,
        Err(payload) => {
            log_panic(payload_message(&*payload));
            poison();
            err
        }
    }
}
