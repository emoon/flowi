//! Profiler scopes as RAII guards: a scope closes when its guard drops, including on an early
//! return or an unwind.
//!
//! The profiler keeps a per-thread LIFO stack of open scopes. Guards on one thread must drop in
//! the reverse of the order they were created; dropping them out of order (for example via
//! [`std::mem::drop`] on an outer guard, or by storing guards in a collection) corrupts the
//! readings that follow.

use std::ffi::CStr;

use crate::sys;

/// A named profiler scope, open until the guard drops.
///
/// Prefer the [`perf_scope!`](crate::perf_scope) macro, which names the guard for you - a bare
/// PerfScope::new("...") whose result is not bound drops immediately and measures nothing. Reach for
/// the constructor directly when the name is a runtime choice between several 'static names.
///
/// The name is a &'static CStr (c"my_scope"), borrowed for the whole scope.
///
/// The guard must drop on the thread that opened it.
pub struct PerfScope(u32);

impl PerfScope {
    /// Open a scope named name, closed when the returned guard drops.
    pub fn new(name: &'static CStr) -> PerfScope {
        // SAFETY: the profiler takes a borrowed, NUL-terminated name and copies what it needs; a
        // 'static CStr satisfies that for the whole scope.
        PerfScope(unsafe { sys::fl_perf_scope_begin(name.as_ptr()) })
    }
}

impl Drop for PerfScope {
    fn drop(&mut self) {
        // SAFETY: closes the scope this handle opened, exactly once.
        unsafe { sys::fl_perf_scope_end(self.0) }
    }
}

/// The per-frame profiler scope, open until the guard drops. Bracket one iteration of a host's frame
/// loop with it: the profiler resets its per-frame scope stack at the begin and publishes the frame's
/// readings at the end.
///
/// A host driving [`Application::run`](crate::Application::run) never needs this - the run loop opens
/// and closes the frame itself. It is for a host that owns its own loop
/// ([`Application::tick`](crate::Application::tick) / embedded mode).
pub struct PerfFrame(());

impl PerfFrame {
    /// Open the per-frame scope, closed when the returned guard drops.
    pub fn new() -> PerfFrame {
        // SAFETY: a plain begin on the process-global profiler; the guard's drop is the matching end.
        unsafe { sys::fl_perf_scope_frame_begin() }
        PerfFrame(())
    }
}

impl Default for PerfFrame {
    fn default() -> Self {
        PerfFrame::new()
    }
}

impl Drop for PerfFrame {
    fn drop(&mut self) {
        // SAFETY: closes the frame scope this guard opened, exactly once.
        unsafe { sys::fl_perf_scope_frame_end() }
    }
}

/// Open a [`PerfScope`] named by a string literal, closed at the end of the enclosing block.
///
/// ```ignore
/// fn update(&mut self) {
///     perf_scope!("frontend_update");
///     // ...measured until this function returns...
/// }
/// ```
///
/// The guard binding is the macro's, so there is no let _scope = to write. Several invocations may
/// share one block; each owns its own guard and they close in reverse order.
#[macro_export]
macro_rules! perf_scope {
    ($name:literal) => {
        // The const block checks for an interior NUL at compile time.
        let _perf_scope = $crate::PerfScope::new(
            const {
                match ::core::ffi::CStr::from_bytes_with_nul(
                    ::core::concat!($name, "\0").as_bytes(),
                ) {
                    ::core::result::Result::Ok(name) => name,
                    ::core::result::Result::Err(_) => {
                        ::core::panic!("a perf scope name must not contain a NUL byte")
                    }
                }
            },
        );
    };
}

#[cfg(test)]
mod tests {
    use super::{PerfFrame, PerfScope};
    use crate::sys;

    /// Open and immediately close a probe scope, returning the stack depth the profiler reported for it -
    /// which is the depth of whatever guards are currently open. The profiler's state is thread-local and each
    /// test runs on its own thread, so this reads only what this test opened.
    fn depth() -> u32 {
        // SAFETY: a balanced begin/end pair on the calling thread's profiler state.
        unsafe {
            let index = sys::fl_perf_scope_begin(c"probe".as_ptr());
            sys::fl_perf_scope_end(index);
            index
        }
    }

    #[test]
    fn scope_guards_nest_and_unwind_in_order() {
        let _frame = PerfFrame::new();
        assert_eq!(depth(), 0, "a fresh frame starts at depth 0");

        {
            perf_scope!("outer");
            assert_eq!(depth(), 1, "the macro's guard opened a scope");

            {
                let _inner = PerfScope::new(c"inner");
                assert_eq!(depth(), 2, "a nested guard opens inside the outer one");
            }
            assert_eq!(
                depth(),
                1,
                "the inner guard closed when its binding went out of scope"
            );
        }
        assert_eq!(depth(), 0, "the outer guard closed with its block");
    }
}
