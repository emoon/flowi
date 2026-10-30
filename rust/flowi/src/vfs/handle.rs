//! Typed async handles over the flowi VFS ticket system.
//!
//! An async launch returns a recycled u32 ticket; a [`Handle<T>`] is that ticket with
//! ownership and a payload type, and allocates nothing.
//!
//! A [`Handle`] is not Copy and not Clone, and it closes its ticket exactly when
//! it drops. The close is non-blocking whether or not the operation has finished: a
//! still-running operation is freed once its job is done. A failed launch is [`None`],
//! so a live [`Handle`] always holds a real ticket.
//!
//! A [`Handle`] is !Send + !Sync by construction (its payload marker is a raw
//! pointer): it belongs to the thread that launched its operation.

use core::future::{Future, IntoFuture};
use core::marker::PhantomData;
use core::pin::Pin;
use core::task::Context;

use crate::sys::FlError;

use crate::vfs::ffi;

pub use core::task::Poll;

/// A result type a completed VFS ticket drains into: the one hook a payload
/// implements to turn `vfs_get_*` output into an owned Rust value.
///
/// Implemented by the VFS surface's result types (file bytes, directory listing).
/// A [`Handle`] is only minted by this crate's launch calls, so an outside impl is
/// never used.
pub trait Payload: Sized {
    /// Drain the completed operation behind ticket into an owned value. Called
    /// exactly once, only after fl_vfs_is_ready(ticket) reported true; must copy
    /// out anything it wants to keep (the C side frees its buffers on close).
    fn drain(ticket: u32) -> Result<Self, FlError>;
}

/// An owning token onto one asynchronous VFS operation: the C u32 ticket, typed.
///
/// Poll it with [`poll`](Handle::poll) until it returns [`Poll::Ready`]. Dropping
/// it - at any point, before or after completion - closes the ticket without
/// blocking. !Send + !Sync, and neither Copy nor Clone.
pub struct Handle<T: Payload> {
    ticket: u32,
    /// Set once the result is drained; another poll then panics.
    done: bool,
    /// Carries the payload type and makes the handle !Send + !Sync.
    _marker: PhantomData<*const T>,
}

impl<T: Payload> Handle<T> {
    /// Wrap a successfully-launched operation's ticket, which must be live and
    /// unclosed - the value the launch returned.
    #[inline]
    pub(crate) fn from_ticket(ticket: u32) -> Handle<T> {
        Handle {
            ticket,
            done: false,
            _marker: PhantomData,
        }
    }

    /// Non-blocking check for completion.
    ///
    /// Returns [`Poll::Pending`] while the operation runs and [`Poll::Ready`] with
    /// its `Result<T, FlError>` the first time it observes completion. Polling
    /// again after a [`Poll::Ready`] panics: the result is yielded exactly once.
    pub fn poll(&mut self) -> Poll<Result<T, FlError>> {
        assert!(!self.done, "handle polled after completion");
        // SAFETY: ticket is our live, unclosed ticket (from_ticket contract).
        if !unsafe { ffi::fl_vfs_is_ready(self.ticket) } {
            return Poll::Pending;
        }
        self.done = true;
        Poll::Ready(T::drain(self.ticket))
    }
}

impl<T: Payload> Drop for Handle<T> {
    fn drop(&mut self) {
        // SAFETY: ticket is our ticket, closed exactly once.
        unsafe { ffi::fl_vfs_close(self.ticket) }
    }
}

/// The [`Future`] form of a [`Handle<T>`], built with [`Handle::into_future`] (so
/// handle.await works). It owns its [`Handle`] and yields the operation's
/// `Result<T, FlError>` when it completes.
///
/// Self-waking: on every [`Poll::Pending`] it wakes its own
/// [`Waker`](core::task::Waker), so its task is re-polled on the next tick.
///
/// Dropping it drops the [`Handle`], cancelling the operation.
pub struct HandleFuture<T: Payload> {
    /// The in-flight handle, [`None`] once it has yielded.
    handle: Option<Handle<T>>,
}

impl<T: Payload> Future for HandleFuture<T> {
    type Output = Result<T, FlError>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let handle = self
            .handle
            .as_mut()
            .expect("HandleFuture polled after completion");
        match handle.poll() {
            Poll::Ready(result) => {
                self.handle = None;
                Poll::Ready(result)
            }
            Poll::Pending => {
                // Nothing else wakes this task, so re-arm for the next tick.
                cx.waker().wake_by_ref();
                Poll::Pending
            }
        }
    }
}

impl<T: Payload> IntoFuture for Handle<T> {
    type Output = Result<T, FlError>;
    type IntoFuture = HandleFuture<T>;

    /// Consume the handle into its awaitable [`HandleFuture`]; dropping the future
    /// cancels the operation.
    #[inline]
    fn into_future(self) -> HandleFuture<T> {
        HandleFuture { handle: Some(self) }
    }
}
