//! The closure-transported read: [`Mount::read_with`] leaves a transform for the
//! worker in a slot, and the worker leaves the transformed value there for the poller.

use super::*;

/// The transform's slot: where [`Mount::read_with`] leaves the closure for the
/// worker, and where the worker leaves the value for the poller.
///
/// Boxed and lent to the VFS as the read's user_data; [`map_release`] frees it. The
/// Mutex publishes the worker's write to the polling thread; access never overlaps.
struct MapSlot<T> {
    state: Mutex<MapState<T>>,
}

/// The caller's transform, boxed so the trampoline needs no F type parameter.
type MapFn<T> = Box<dyn FnOnce(&[u8]) -> T + Send>;

/// The slot's three states: waiting to run, holding a value, and emptied.
enum MapState<T> {
    /// The transform, not yet run.
    Pending(MapFn<T>),
    /// What the transform produced.
    Done(T),
    /// The value has been taken, or the transform never ran.
    Taken,
}

/// The VfsReadCallback [`Mount::read_with`] installs: run the caller's closure on
/// the worker that finished the read, and leave its value in the slot.
///
/// Returns the data pointer it was given unchanged, so the VFS keeps ownership of
/// its buffer (a different pointer would transfer ownership).
extern "C" fn map_trampoline<T>(
    data: *mut c_void,
    size: i64,
    user_data: *mut c_void,
) -> sys::VfsData {
    let failed = |success| sys::VfsData {
        data: data.cast::<u8>(),
        size,
        success,
        error_message: RawStr::EMPTY,
    };
    if user_data.is_null() {
        return failed(false);
    }
    // SAFETY: user_data is the `MapSlot<T>` box read_with lent to exactly this
    // read, and the VFS frees it (map_release) only after this callback has run.
    let slot = unsafe { &*user_data.cast::<MapSlot<T>>() };

    // The closure is taken out before running so user code never runs under the lock.
    let Ok(mut guard) = slot.state.lock() else {
        return failed(false);
    };
    let MapState::Pending(transform) = core::mem::replace(&mut *guard, MapState::Taken) else {
        return failed(false);
    };
    drop(guard);

    // A null data pointer means the read failed; an empty file still has a pointer.
    if data.is_null() {
        // The closure may capture a panicking Drop; it must not unwind out of here.
        if catch_unwind(AssertUnwindSafe(move || drop(transform))).is_err() {
            log::error!("VFS read transform panicked while dropping; reporting the read as failed");
        }
        return failed(false);
    }
    let bytes = if size <= 0 {
        &[][..]
    } else {
        // SAFETY: the VFS hands the callback size readable bytes at data.
        unsafe { std::slice::from_raw_parts(data.cast::<u8>(), size as usize) }
    };
    // A panicking transform is reported as a failed read.
    let Ok(value) = catch_unwind(AssertUnwindSafe(|| transform(bytes))) else {
        log::error!("VFS read transform panicked; reporting the read as failed");
        return failed(false);
    };

    let Ok(mut guard) = slot.state.lock() else {
        return failed(false);
    };
    *guard = MapState::Done(value);
    failed(true)
}

/// The VfsReleaseCallback [`Mount::read_with`] installs: the VFS runs it exactly
/// once when it frees the read's handle, after the job, and this frees the slot.
extern "C" fn map_release<T>(user_data: *mut c_void) {
    if user_data.is_null() {
        return;
    }
    // SAFETY: user_data is the `MapSlot<T>` box read_with lent to this read, handed
    // back exactly once, here.
    let slot = unsafe { Box::from_raw(user_data.cast::<MapSlot<T>>()) };
    // The value or the transform may carry a panicking Drop.
    if catch_unwind(AssertUnwindSafe(move || drop(slot))).is_err() {
        log::error!("VFS read slot panicked while dropping; the rest of it was not dropped");
    }
}

/// An in-flight [`Mount::read_with`]: the read's ticket plus the slot its transform
/// leaves a value in.
///
/// Owning, like [`Handle`]: dropping it closes the operation without blocking. The
/// transform and any value it produced are freed when the VFS frees the handle -
/// at the drop for a read that has completed, on the worker as the job ends for one
/// dropped mid-flight. Poll it with [`MappedRead::poll`].
///
/// The VFS may also free the handle under a live MappedRead when its mount is
/// closed. Such a read polls [`Poll::Pending`] from then on, as any [`Handle`]
/// does, and never touches its slot again.
pub struct MappedRead<T> {
    handle: Handle<Mapped>,
    /// Lent to the VFS and freed by [`map_release`] when the VFS frees the handle.
    /// Only dereferenced right after fl_vfs_is_ready observed the handle registered.
    slot: *mut MapSlot<T>,
}

impl<T> MappedRead<T> {
    /// Non-blocking check for completion, yielding the transform's value the first
    /// time it observes the read finish.
    ///
    /// [`Poll::Pending`] while the read runs. A read that failed, or whose
    /// transform failed, yields [`Err`]. Polling again after a [`Poll::Ready`]
    /// panics - the value is yielded exactly once, like a fused future.
    pub fn poll(&mut self) -> Poll<Result<T, FlError>> {
        match self.handle.poll() {
            Poll::Pending => Poll::Pending,
            Poll::Ready(Err(error)) => Poll::Ready(Err(error)),
            Poll::Ready(Ok(Mapped)) => Poll::Ready(self.take_value()),
        }
    }

    /// Take what the transform produced, or the generic error when it left nothing.
    /// Called only from [`poll`](Self::poll), right after the handle polled ready.
    fn take_value(&mut self) -> Result<T, FlError> {
        // SAFETY: handle.poll just saw fl_vfs_is_ready true, so the VFS held the handle
        // and had not run map_release. Nothing frees it before this returns: the
        // release hook runs only once the ticket's id is closed or its mount is, and
        // ours is not closed while its Handle lives; fl_vfs_close on this ticket and
        // fl_vfs_mount_close run only on this thread, since Handle, Mount and
        // OwnedMount are !Send; and the one call since the check (fl_vfs_get_data, in
        // Mapped::drain) frees nothing.
        let slot = unsafe { &*self.slot };
        let Ok(mut guard) = slot.state.lock() else {
            return Err(FlError::GenericError);
        };
        match core::mem::replace(&mut *guard, MapState::Taken) {
            MapState::Done(value) => Ok(value),
            MapState::Pending(_) | MapState::Taken => Err(FlError::GenericError),
        }
    }
}

/// The payload of a [`Mount::read_with`] ticket: only whether the read and the
/// transform succeeded. The value travels through the slot.
pub(crate) struct Mapped;

impl Payload for Mapped {
    fn drain(ticket: u32) -> Result<Mapped, FlError> {
        // SAFETY: as VfsData::drain.
        let data = unsafe { ffi::fl_vfs_get_data(ticket) };
        if data.success {
            Ok(Mapped)
        } else {
            Err(FlError::GenericError)
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// The launch side of the closure-transported read.
impl Mount {
    /// Launch an async read of path whose bytes are handed to transform on the
    /// worker that completes it, so decode or parse work happens off the frame
    /// loop. The returned [`MappedRead`] polls to whatever transform produced.
    ///
    /// The transform is freed whether the read completes, fails, or is dropped
    /// mid-flight, and never while the worker can still reach it. A panic inside it
    /// is caught and surfaced as a failed read. [`Mount::read`] is the plain-bytes read.
    pub fn read_with<T, F>(&self, path: &str, transform: F) -> Option<MappedRead<T>>
    where
        T: Send + 'static,
        F: FnOnce(&[u8]) -> T + Send + 'static,
    {
        let slot = Box::into_raw(Box::new(MapSlot {
            state: Mutex::new(MapState::Pending(Box::new(transform))),
        }));
        // SAFETY: a live mount; path is borrowed for the call only. slot is lent to
        // the VFS, which runs map_trampoline on it at most once and map_release on
        // it exactly once when it frees the handle, after the job.
        let ticket = unsafe {
            ffi::fl_vfs_mount_read_all_with_options(
                self.raw,
                sys::RawStr::borrow(path),
                Some(map_trampoline::<T>),
                slot.cast(),
                Some(map_release::<T>),
                FL_VFS_HANDLE_INVALID,
            )
        };
        let Some(handle) = self.ticket_handle::<Mapped>(ticket) else {
            // A refused launch minted no handle, so the release hook never runs.
            // SAFETY: slot is the Box::into_raw above, and the VFS did not take it.
            drop(unsafe { Box::from_raw(slot) });
            return None;
        };
        Some(MappedRead { handle, slot })
    }
}
