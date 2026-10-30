//! The mount surface: the factory, the borrowed and owning mounts, the worker view,
//! and the values a mount is opened with and reports.

use super::*;

/// What a mount is opened with. [`Default`] is the driver's own cache location and no
/// revalidation.
#[derive(Copy, Clone, Debug, Default)]
pub struct MountOptions<'a> {
    /// Where a caching driver writes its downloads. Empty selects the default
    /// cache; a directory given here must already exist.
    pub cache_dir: &'a str,
    /// Revalidate cached files against the server (ETag) before using them - for
    /// a mutable remote resource. false uses the cache as-is.
    pub cache_validation: bool,
}

impl MountOptions<'_> {
    /// Lower to the ABI struct. cache_dir is borrowed for the duration of the mount call.
    pub(super) fn to_sys(self) -> sys::VfsMountOptions {
        sys::VfsMountOptions {
            cache_dir: sys::RawStr::borrow(self.cache_dir),
            cache_validation: self.cache_validation,
        }
    }
}

/// Why a mount could not be opened.
///
/// This is the refusal: the request never became a mount. A mount that opens
/// and then fails while its driver resolves it is a live [`Mount`] reporting
/// [`MountStatus::Error`], not an error here.
#[doc(inline)]
pub use self::mount_error_decl::MountError;

/// Generates the enum, its [`Display`](core::fmt::Display), the exhaustive status mapping
/// and the test table, from one list of `Variant = Status, "message";`.
macro_rules! mount_errors {
    ($( $(#[$doc:meta])* $variant:ident = $status:ident, $text:literal; )*) => {
        /// Why a mount could not be opened.
        ///
        /// This is the refusal: the request never became a mount. A mount that opens
        /// and then fails while its driver resolves it is a live [`Mount`] reporting
        /// [`MountStatus::Error`], not an error here.
        #[derive(Copy, Clone, PartialEq, Eq, Debug)]
        pub enum MountError {
            $( $(#[$doc])* $variant, )*
        }

        impl core::fmt::Display for MountError {
            fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
                f.write_str(match self {
                    $( MountError::$variant => $text, )*
                })
            }
        }

        impl MountError {
            /// Map a mount result's status. Success yields [`None`] - the caller has a
            /// mount pointer to use instead.
            pub(super) fn from_status(status: sys::VfsMountErrorStatus) -> Option<MountError> {
                match status {
                    sys::VfsMountErrorStatus::Success => None,
                    $( sys::VfsMountErrorStatus::$status => Some(MountError::$variant), )*
                }
            }
        }

        /// Every refusal as (status, variant, message).
        #[cfg(test)]
        pub(crate) const MOUNT_ERRORS: &[(sys::VfsMountErrorStatus, MountError, &str)] = &[
            $( (sys::VfsMountErrorStatus::$status, MountError::$variant, $text), )*
        ];
    };
}

mod mount_error_decl {
    use super::*;

    mount_errors! {
        /// The source path is empty or malformed.
        InvalidPath = InvalidPath, "invalid source path";
        /// No VFS driver claimed the source.
        PluginNotFound = PluginNotFound, "no VFS driver for this source";
        /// A driver claimed it and failed to open it - missing, unreadable, denied.
        MountFailed = MountFailed, "the driver could not open the source";
        /// Allocation failed.
        OutOfMemory = OutOfMemory, "out of memory";
        /// The VFS has not been initialized.
        NotInitialized = NotInitialized, "the VFS is not initialized";
    }
}

#[cfg(test)]
pub(super) use self::mount_error_decl::MOUNT_ERRORS;

impl std::error::Error for MountError {}

/// Mount readiness - the coarse state [`Mount::status`] reports.
#[derive(Copy, Clone, PartialEq, Eq, Debug)]
pub enum MountStatus {
    /// The mount operation is still in flight.
    Pending,
    /// Ready for reads and listings.
    Ready,
    /// The mount failed.
    Error,
}

/// What a mount reports about itself, as owned values - the snapshot
/// [`Mount::info`] takes.
///
/// [`Mount::source_path`] is the zero-copy form of the path field.
#[derive(Clone, PartialEq, Eq, Debug)]
pub struct MountInfo {
    /// The source path the mount was created from.
    pub path: String,
    /// Whether the mount is ready for operations.
    pub is_ready: bool,
}

/// A borrowed VFS mount, and the surface every VFS operation lives on.
///
/// Host-owned and opaque. [`Vfs::mount`] hands back the owning [`OwnedMount`],
/// which derefs to this; a mount that came from C is wrapped with
/// [`Mount::from_raw`].
///
/// Not [`Clone`]: share a mount by borrowing it (&Mount), or, for a mount that lives
/// for the whole run, through the &'static Mount [`OwnedMount::leak`] returns.
///
/// !Send + !Sync; [`Mount::worker_view`] is the way to a job body.
pub struct Mount {
    pub(crate) raw: *mut sys::FlVfsMount,
    _marker: PhantomData<*const ()>,
}

impl Mount {
    /// Wrap a raw `FlVfsMount*` (as an opaque pointer) obtained from the flowi VFS -
    /// a mount a C caller opened and still owns.
    ///
    /// # Safety
    /// raw must be a valid `FlVfsMount*` that outlives this Mount and every
    /// operation launched against it.
    #[inline]
    pub unsafe fn from_raw(raw: *mut c_void) -> Mount {
        Mount::from_raw_ptr(raw as *mut sys::FlVfsMount)
    }

    /// Wrap an already-typed mount pointer. The validity promise belongs to whoever
    /// hands the pointer in: [`Mount::from_raw`]'s caller or [`Vfs::mount_with`].
    #[inline]
    fn from_raw_ptr(raw: *mut sys::FlVfsMount) -> Mount {
        Mount {
            raw,
            _marker: PhantomData,
        }
    }

    /// The raw `FlVfsMount*`, for passing to a C API that takes one. Valid for as long
    /// as the mount is open.
    #[inline]
    pub fn as_raw(&self) -> *mut c_void {
        self.raw.cast()
    }

    /// Everything the mount reports about itself, copied out.
    pub fn info(&self) -> MountInfo {
        // SAFETY: a live mount (the from_raw / Vfs::mount contract).
        let info = unsafe { ffi::fl_vfs_mount_get_info(self.raw) };
        MountInfo {
            path: info.path.into_string(),
            is_ready: info.is_ready,
        }
    }

    /// Where this mount was created from - the source path [`Vfs::mount`] was given.
    ///
    /// Borrowed from the mount, not copied. [`Mount::info`] is the owning form.
    #[inline]
    pub fn source_path(&self) -> &str {
        // SAFETY: a live mount. The path is the mount's own source_path field, immutable
        // for the mount's lifetime, so tying the borrow to &self is sound.
        unsafe { ffi::fl_vfs_mount_get_info(self.raw).path.as_str() }
    }

    /// Whether the mount has finished coming up, failed, or is still in flight.
    /// Mounting is asynchronous, so a fresh mount reports [`MountStatus::Pending`]
    /// until its driver has resolved it.
    pub fn status(&self) -> MountStatus {
        // SAFETY: a live mount.
        match unsafe { ffi::fl_vfs_mount_get_status(self.raw) } {
            sys::VfsMountStatus::Ready => MountStatus::Ready,
            sys::VfsMountStatus::Error => MountStatus::Error,
            sys::VfsMountStatus::Pending => MountStatus::Pending,
        }
    }

    /// True once the mount is ready for reads and listings.
    #[inline]
    pub fn is_ready(&self) -> bool {
        // SAFETY: a live mount.
        unsafe { ffi::fl_vfs_mount_is_ready(self.raw) }
    }

    /// The job the mount's own bring-up runs as, so work that reads through this
    /// mount can be scheduled after it (`crate::Jobs::add_after`) rather than
    /// polling [`is_ready`](Mount::is_ready).
    ///
    /// A driver that resolved the mount without scheduling anything reports
    /// [`JobHandle::INVALID`](crate::JobHandle::INVALID), and a handle whose job has
    /// already finished is a recycled one; add_after treats both as "no dependency",
    /// so the same call is correct whatever the mount's state.
    #[inline]
    pub fn job_handle(&self) -> crate::JobHandle {
        // SAFETY: a live mount.
        unsafe { ffi::fl_vfs_mount_get_job_handle(self.raw) }
    }

    /// Launch an async read of path within this mount. [`None`] if the C side
    /// refused to start the operation; otherwise a [`Handle`] that polls to the
    /// file's bytes.
    pub fn read(&self, path: &str) -> Option<Handle<VfsData>> {
        // SAFETY: a live mount; path's bytes are borrowed only for the duration
        // of this call (the C side copies what it needs), and the invalid
        // reuse_handle requests a fresh ticket.
        let ticket = unsafe {
            ffi::fl_vfs_mount_read_all_with_options(
                self.raw,
                sys::RawStr::borrow(path),
                None,
                ptr::null_mut(),
                None,
                FL_VFS_HANDLE_INVALID,
            )
        };
        self.ticket_handle(ticket)
    }

    /// Launch an async listing of path within this mount. `depth` is ignored: the
    /// listing is always one level. [`None`] if the C side refused to start it,
    /// otherwise a [`Handle`] that polls to the listing.
    pub fn listing(&self, path: &str, depth: i32) -> Option<Handle<FileList>> {
        // SAFETY: as read; fl_vfs_get_listing borrows path for the call only.
        let ticket = unsafe { ffi::fl_vfs_get_listing(self.raw, sys::RawStr::borrow(path), depth) };
        self.ticket_handle(ticket)
    }

    /// Wrap a launch's ticket, mapping the invalid sentinel to [`None`].
    #[inline]
    pub(crate) fn ticket_handle<T: Payload>(&self, ticket: FlVfsHandle) -> Option<Handle<T>> {
        (ticket != FL_VFS_HANDLE_INVALID).then(|| Handle::from_ticket(ticket))
    }

    /// A Send view of this mount, carrying the blocking form of the same
    /// operations - what a job body reads through.
    ///
    /// A job body must read through this view rather than polling a [`Handle`]: the
    /// VFS runs its work on the same job pool, so a worker spinning on a handle can
    /// deadlock. On a worker each operation of the view completes before its launch
    /// returns. Do not use the view on the frame loop: every call blocks.
    ///
    /// # Safety
    /// The mount must stay open until every holder of the returned view - and every
    /// job body reading through one - is finished with it. The view is Send and
    /// carries no lifetime, so nothing but this promise ties it to the mount.
    #[inline]
    pub unsafe fn worker_view(&self) -> WorkerMount {
        WorkerMount {
            mount: Mount::from_raw_ptr(self.raw),
        }
    }
}

/// A mount this process opened and is responsible for closing.
///
/// The owning form [`Vfs::mount`] hands back. It derefs to [`Mount`] and closes the
/// mount on drop.
pub struct OwnedMount {
    mount: Mount,
}

impl OwnedMount {
    /// Give up ownership: the mount stays open for the rest of the process, reachable
    /// through the returned &'static Mount.
    pub fn leak(self) -> &'static Mount {
        let mount = Mount::from_raw_ptr(self.mount.raw);
        core::mem::forget(self);
        Box::leak(Box::new(mount))
    }
}

impl Deref for OwnedMount {
    type Target = Mount;

    #[inline]
    fn deref(&self) -> &Mount {
        &self.mount
    }
}

impl Drop for OwnedMount {
    fn drop(&mut self) {
        // SAFETY: the mount came from Vfs::mount and is closed exactly once.
        unsafe { ffi::fl_vfs_mount_close(self.mount.raw) };
    }
}

/// A mount view that may cross to a worker thread, carrying the blocking form
/// of the mount operations. Minted by [`Mount::worker_view`], which is where the
/// safety contract lives.
///
/// It derefs to [`Mount`], so the queries (info, status, source_path, ...) are
/// all reachable; its read and listing operations shadow their async namesakes and
/// block until the result is ready. Never use it on the frame loop.
///
/// Each operation launches the operations it needs and closes them before returning.
/// Invariant: an operation only waits on, or chains onto, an operation it launched
/// itself in the same call, which on a worker has already completed.
pub struct WorkerMount {
    mount: Mount,
}

// SAFETY: the C VFS is internally synchronized, so the operations are callable from any
// thread, and each waits only on tickets it launched in the same call. The mount's
// lifetime is the promise Mount::worker_view takes from its caller. Not Sync, so a
// &Mount (which is !Sync) cannot be reached from another thread through this.
unsafe impl Send for WorkerMount {}

impl Clone for WorkerMount {
    /// Duplicate the view, under the same promise [`Mount::worker_view`] took.
    fn clone(&self) -> WorkerMount {
        WorkerMount {
            mount: Mount::from_raw_ptr(self.mount.raw),
        }
    }
}

impl Deref for WorkerMount {
    type Target = Mount;

    #[inline]
    fn deref(&self) -> &Mount {
        &self.mount
    }
}

impl WorkerMount {
    /// Read the whole of path, waiting for the bytes.
    pub fn read(&self, path: &str) -> Result<VfsData, FlError> {
        let mount = &self.mount;
        // SAFETY: a live mount (the worker_view promise); path is borrowed for
        // the launch only, and the ticket is closed exactly once below.
        unsafe {
            let ticket = ffi::fl_vfs_mount_read_all_with_options(
                mount.raw,
                sys::RawStr::borrow(path),
                None,
                ptr::null_mut(),
                None,
                FL_VFS_HANDLE_INVALID,
            );
            self.drain_blocking::<VfsData>(ticket)
        }
    }

    /// Read at most max_bytes from the start of path, waiting for the bytes.
    ///
    /// A file smaller than max_bytes is not an error: the returned buffer holds
    /// however much was there.
    pub fn read_prefix(&self, path: &str, max_bytes: usize) -> Result<Vec<u8>, FlError> {
        if max_bytes == 0 {
            return Ok(Vec::new());
        }
        let mount = &self.mount;
        let mut buffer = vec![0u8; max_bytes];
        // SAFETY: a live mount; path is borrowed for the open only. buffer
        // outlives the wait below, which is what fl_vfs_read's no-copy contract
        // requires, and both tickets are closed on every path out.
        unsafe {
            let file = ffi::fl_vfs_mount_open(mount.raw, sys::RawStr::borrow(path), VFS_OPEN_READ);
            if file == FL_VFS_HANDLE_INVALID {
                return Err(FlError::GenericError);
            }
            let read =
                ffi::fl_vfs_read(file, buffer.as_mut_ptr().cast::<c_void>(), max_bytes as i64);
            if read == FL_VFS_HANDLE_INVALID {
                ffi::fl_vfs_close(file);
                return Err(FlError::GenericError);
            }
            ffi::fl_vfs_wait(read);
            let data = ffi::fl_vfs_get_data(read);
            let read_bytes = if data.success && data.size > 0 {
                (data.size as usize).min(max_bytes)
            } else if data.success {
                0
            } else {
                ffi::fl_vfs_close(read);
                ffi::fl_vfs_close(file);
                return Err(FlError::GenericError);
            };
            ffi::fl_vfs_close(read);
            ffi::fl_vfs_close(file);
            buffer.truncate(read_bytes);
        }
        Ok(buffer)
    }

    /// Read the whole of path in order, at most chunk_size bytes at a time,
    /// handing each piece to each as it arrives. Memory stays at one chunk
    /// however large the file is.
    pub fn read_chunks(
        &self,
        path: &str,
        chunk_size: usize,
        mut each: impl FnMut(&[u8]),
    ) -> Result<(), FlError> {
        if chunk_size == 0 {
            return Err(FlError::GenericError);
        }
        let mount = &self.mount;
        let mut buffer = vec![0u8; chunk_size];
        // SAFETY: a live mount; path is borrowed for the open only. buffer
        // outlives every wait below, which is what fl_vfs_read's no-copy contract
        // requires, and every ticket is closed on every path out.
        let file =
            unsafe { ffi::fl_vfs_mount_open(mount.raw, sys::RawStr::borrow(path), VFS_OPEN_READ) };
        if file == FL_VFS_HANDLE_INVALID {
            return Err(FlError::GenericError);
        }
        let result = loop {
            // SAFETY: file is the open handle above, and buffer holds chunk_size bytes.
            let read = unsafe {
                ffi::fl_vfs_read(
                    file,
                    buffer.as_mut_ptr().cast::<c_void>(),
                    chunk_size as i64,
                )
            };
            if read == FL_VFS_HANDLE_INVALID {
                break Err(FlError::GenericError);
            }
            // SAFETY: read is a live ticket, waited on and closed exactly once.
            let data = unsafe {
                ffi::fl_vfs_wait(read);
                let data = ffi::fl_vfs_get_data(read);
                ffi::fl_vfs_close(read);
                data
            };
            if !data.success {
                break Err(FlError::GenericError);
            }
            let read_bytes = (data.size.max(0) as usize).min(chunk_size);
            if read_bytes == 0 {
                break Ok(());
            }
            each(&buffer[..read_bytes]);
        };
        // SAFETY: file is closed exactly once, after its last read.
        unsafe { ffi::fl_vfs_close(file) };
        result
    }

    /// List path, waiting for the entries. `depth` is ignored: the listing is always one level.
    pub fn listing(&self, path: &str, depth: i32) -> Result<FileList, FlError> {
        let mount = &self.mount;
        // SAFETY: a live mount; path is borrowed for the launch only, and the
        // ticket is closed exactly once below.
        unsafe {
            let ticket = ffi::fl_vfs_get_listing(mount.raw, sys::RawStr::borrow(path), depth);
            self.drain_blocking::<FileList>(ticket)
        }
    }

    /// Wait for ticket, drain it, and close it.
    ///
    /// # Safety
    /// ticket is a ticket this view just launched, not yet closed.
    unsafe fn drain_blocking<T: Payload>(&self, ticket: FlVfsHandle) -> Result<T, FlError> {
        if ticket == FL_VFS_HANDLE_INVALID {
            return Err(FlError::GenericError);
        }
        // SAFETY: the caller's contract - ticket was just launched by this view and is not yet closed, so
        // it is live for the wait and the drain, and this is the one place it is closed.
        unsafe {
            ffi::fl_vfs_wait(ticket);
            let drained = T::drain(ticket);
            ffi::fl_vfs_close(ticket);
            drained
        }
    }
}

/// The mount factory. There is one VFS per process, so this is a namespace with no
/// values; reads and listings are methods on the [`Mount`] it hands back.
pub enum Vfs {}

impl Vfs {
    /// Bring the VFS up against arena and register the built-in LocalFS driver,
    /// reporting whether it came up.
    ///
    /// Only needed when nothing else brings the VFS up (for example a test binary
    /// without an application). A second call is refused (false).
    ///
    /// arena must outlive every mount opened against it - in practice the
    /// process-wide arena `crate::init` returns.
    #[inline]
    pub fn init(arena: &crate::Arena) -> bool {
        // SAFETY: the arena outlives the call; its lifetime past the call is this
        // function's documented contract.
        unsafe { ffi::fl_vfs_init(arena.as_raw()) }
    }

    /// Register every archive driver this build of the library contains, ahead of
    /// the LocalFS fallback; with none, this does nothing.
    ///
    /// Call it once, after the VFS is up. Each call registers the drivers again.
    #[inline]
    pub fn register_builtin_drivers() {
        // SAFETY: no arguments; registering before the VFS is up changes nothing.
        unsafe { ffi::fl_vfs_register_builtin_drivers() }
    }

    /// Mount source_path - a directory, an archive, or a URL; the VFS picks the
    /// driver. Uses the default [`MountOptions`].
    ///
    /// Mounting is asynchronous: this returns as soon as the request is accepted, and
    /// the mount reports [`MountStatus::Pending`] until its driver resolves it. A
    /// mount that fails later still comes back here as a live handle - poll
    /// [`Mount::status`] for that. The [`Err`] is the request being refused outright.
    pub fn mount(source_path: &str) -> Result<OwnedMount, MountError> {
        Vfs::mount_with(source_path, MountOptions::default())
    }

    /// Mount source_path with explicit options - a cache directory of your own,
    /// or revalidation against the server.
    pub fn mount_with(
        source_path: &str,
        options: MountOptions<'_>,
    ) -> Result<OwnedMount, MountError> {
        // SAFETY: source_path and options.cache_dir are borrowed for the call
        // only; the C side copies what it keeps.
        let result = unsafe {
            ffi::fl_vfs_mount_with_options(sys::RawStr::borrow(source_path), options.to_sys())
        };
        if let Some(error) = MountError::from_status(result.status) {
            return Err(error);
        }
        // A success status with a null mount is reported as a driver failure.
        if result.mount.is_null() {
            return Err(MountError::MountFailed);
        }
        Ok(OwnedMount {
            mount: Mount::from_raw_ptr(result.mount),
        })
    }
}
