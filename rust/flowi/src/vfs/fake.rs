//! The per-thread stand-in for the C VFS that [`ffi`] dispatches to under cfg(test).
//!
//! The table is generated from [`vfs_slots!`](super::slots::vfs_slots). The fixture the cases
//! drive it with - the per-thread [`FakeState`], the slot implementations and `fake_table` - lives
//! here too.

use super::payload::OwnedEntry;
use super::slots::vfs_slots;
use super::*;
use std::cell::RefCell;

/// Generates everything about the fake's table that is derivable from the slot list: the struct of
/// function pointers, a panicking stub per slot, and the all-stubs base a test builds on.
macro_rules! define_fake_api {
    ($($name:ident($($arg:ident: $ty:ty),* $(,)?) $(-> $ret:ty)?;)*) => {
        /// One slot per C entry point; unfilled slots panic.
        #[derive(Copy, Clone)]
        pub(crate) struct VfsApi {
            $(pub(crate) $name: unsafe extern "C" fn($($ty),*) $(-> $ret)?,)*
        }

        /// Slot fillers for a table under construction.
        mod unimplemented_slots {
            use super::*;
            $(
                pub(super) unsafe extern "C" fn $name($(_: $ty),*) $(-> $ret)? {
                    unimplemented!(concat!("the test's VFS table has no ", stringify!($name)))
                }
            )*
        }

        impl VfsApi {
            /// A table every slot of which panics. The base a test builds its fake on,
            /// so it fills in only the calls its scenario actually drives.
            pub(crate) fn unimplemented() -> VfsApi {
                VfsApi {
                    $($name: unimplemented_slots::$name,)*
                }
            }
        }
    };
}

vfs_slots!(define_fake_api);

thread_local! {
    /// The table this thread's VFS calls land in. Per-thread so tests running
    /// concurrently cannot see each other's fakes.
    static TABLE: RefCell<VfsApi> = RefCell::new(VfsApi::unimplemented());
}

/// Point this thread's VFS calls at api for the rest of the test.
pub(crate) fn install(api: VfsApi) {
    TABLE.with(|table| *table.borrow_mut() = api);
}

/// The table [`ffi`] dispatches through. Copied out rather than borrowed so a
/// slot is free to re-enter (a read callback that itself calls the VFS).
pub(crate) fn table() -> VfsApi {
    TABLE.with(|table| *table.borrow())
}

// The fixture the cases drive the table with: the per-thread FakeState that stands in for an
// async operation, the slot implementations that read and write it, and fake_table().

pub(super) struct FakeState {
    /// is_ready returns false this many polls, then true.
    pub(super) polls_until_ready: i32,
    /// Bytes get_data hands back on success.
    pub(super) data: Vec<u8>,
    /// When set, get_data / get_file_list report failure.
    pub(super) fail: bool,
    /// When set, the launch calls return the invalid sentinel.
    pub(super) launch_invalid: bool,
    /// When set, a launch accepts the read callback but does not run it - the
    /// real VFS's "still in flight" state, which is when a read_with slot is
    /// still holding its untouched transform.
    pub(super) hold_callback: bool,
    /// Entries get_file_list reports.
    pub(super) entries: Vec<OwnedEntry>,
    /// What fl_vfs_mount_with_options reports.
    pub(super) mount_status: sys::VfsMountErrorStatus,
    /// When set, fl_vfs_mount_with_options hands back a null mount whatever its status.
    pub(super) mount_null: bool,
    /// How many times fl_vfs_close was called.
    pub(super) close_count: u32,
    /// How many times fl_vfs_mount_close was called. A closed mount also releases
    /// the tracked read the way the real one unlists every handle of the mount,
    /// and every ticket reads as absent (never ready) from then on.
    pub(super) mount_close_count: u32,
    pub(super) mount_closed: bool,
    /// How many times fl_vfs_wait was called.
    pub(super) wait_count: u32,
    /// The arena the last fl_vfs_init was handed.
    pub(super) init_arena: *mut sys::Arena,
    /// How many times fl_vfs_register_builtin_drivers was called.
    pub(super) builtin_registrations: u32,
    /// Bytes fl_vfs_read copies into the caller's buffer.
    pub(super) prefix: Vec<u8>,
    /// The last size fl_vfs_read was asked for.
    pub(super) prefix_requested: i64,
    /// Where the next fl_vfs_read starts in prefix. An open rewinds it and each read advances it, so
    /// successive reads on one file walk prefix the way they walk a real file.
    pub(super) read_pos: usize,
    /// The names/entries the callback path saw, so a read_with test can
    /// assert the transform ran on the bytes the read produced.
    pub(super) last_callback_size: i64,
    /// What fl_vfs_mount_get_job_handle reports for the mount.
    pub(super) mount_job_handle: u64,
    /// The read-all the fake currently tracks, so the release hook runs when the
    /// real VFS would: at close for a read whose callback has run, and after the
    /// callback for one closed while still held.
    pub(super) read: Option<FakeRead>,
}

pub(super) struct FakeRead {
    callback: sys::VfsReadCallback,
    release: sys::VfsReleaseCallback,
    user_data: *mut c_void,
    callback_ran: bool,
    closed: bool,
}

impl FakeRead {
    /// The hand-back the VFS does when it frees the handle.
    fn release(self) {
        if let Some(release) = self.release {
            // SAFETY: user_data is the launch's own pointer, handed back exactly once.
            unsafe { release(self.user_data) };
        }
    }
}

impl Default for FakeState {
    fn default() -> FakeState {
        FakeState {
            polls_until_ready: 0,
            data: Vec::new(),
            fail: false,
            launch_invalid: false,
            hold_callback: false,
            entries: Vec::new(),
            mount_status: sys::VfsMountErrorStatus::Success,
            mount_null: false,
            close_count: 0,
            mount_close_count: 0,
            mount_closed: false,
            wait_count: 0,
            init_arena: core::ptr::null_mut(),
            builtin_registrations: 0,
            prefix: Vec::new(),
            prefix_requested: 0,
            read_pos: 0,
            last_callback_size: -1,
            mount_job_handle: 0,
            read: None,
        }
    }
}

thread_local! {
    static FAKE: RefCell<FakeState> = RefCell::new(FakeState::default());
    /// The FlVfsEntry array the fake listing points at. Held per-thread so it
    /// outlives the fl_vfs_get_file_list call that returns a pointer into it -
    /// exactly like the C listing's own storage.
    static FAKE_ENTRIES: RefCell<Vec<sys::VfsEntry>> = const { RefCell::new(Vec::new()) };
}

pub(super) fn reset_fake(state: FakeState) {
    fake::install(fake_table());
    FAKE.with(|f| *f.borrow_mut() = state);
    FAKE_ENTRIES.with(|e| e.borrow_mut().clear());
}

pub(super) fn with_fake<R>(f: impl FnOnce(&mut FakeState) -> R) -> R {
    FAKE.with(|state| f(&mut state.borrow_mut()))
}

/// A non-null pointer the fake never dereferences, standing in for a mount.
pub(super) fn fake_mount_ptr() -> *mut sys::FlVfsMount {
    core::ptr::NonNull::<u8>::dangling().as_ptr().cast()
}

unsafe extern "C" fn fake_mount_with_options(
    _path: sys::RawStr,
    _options: sys::VfsMountOptions,
) -> sys::VfsMountResult {
    with_fake(|f| sys::VfsMountResult {
        mount: if f.mount_null || f.mount_status != sys::VfsMountErrorStatus::Success {
            core::ptr::null_mut()
        } else {
            fake_mount_ptr()
        },
        status: f.mount_status,
        error_message: RawStr::EMPTY,
    })
}

unsafe extern "C" fn fake_mount_close(_mount: *mut sys::FlVfsMount) {
    let read = with_fake(|f| {
        f.mount_close_count += 1;
        f.mount_closed = true;
        f.read.take()
    });
    if let Some(read) = read {
        read.release();
    }
}

unsafe extern "C" fn fake_mount_is_ready(_mount: *mut sys::FlVfsMount) -> bool {
    true
}

unsafe extern "C" fn fake_mount_get_status(_mount: *mut sys::FlVfsMount) -> sys::VfsMountStatus {
    sys::VfsMountStatus::Ready
}

unsafe extern "C" fn fake_mount_get_info(_mount: *mut sys::FlVfsMount) -> sys::VfsMountInfo {
    sys::VfsMountInfo {
        path: sys::RawStr::from_static("/fake/source"),
        is_ready: true,
    }
}

unsafe extern "C" fn fake_read_all_with_options(
    _mount: *mut sys::FlVfsMount,
    _path: sys::RawStr,
    callback: sys::VfsReadCallback,
    user_data: *mut c_void,
    release: sys::VfsReleaseCallback,
    _reuse: u32,
) -> u32 {
    if with_fake(|f| f.launch_invalid) {
        return FL_VFS_HANDLE_INVALID;
    }
    with_fake(|f| {
        assert!(
            f.read.is_none(),
            "the fake tracks one read-all at a time; close the previous one first"
        );
        f.read = Some(FakeRead {
            callback,
            release,
            user_data,
            callback_ran: callback.is_none(),
            closed: false,
        })
    });
    // The real VFS runs the callback on a worker, racing the poller's close. The
    // fake serialises the two orders: it runs the callback inline at launch, or,
    // under hold_callback, not until the test runs it after the fact.
    if callback.is_some() && !with_fake(|f| f.hold_callback) {
        let (ptr, size) = with_fake(|f| (f.data.as_ptr() as *mut c_void, f.data.len() as i64));
        run_read_callback(Some((ptr, size)));
    }
    1
}

/// Run the tracked read's callback as the worker does once it reaches the read,
/// with (data, size) or with no data for a read that failed; then, if the read
/// was already closed, hand its user_data back the way the last reference dropping would.
pub(super) fn run_read_callback(data: Option<(*mut c_void, i64)>) {
    let (callback, user_data) = with_fake(|f| {
        let read = f.read.as_mut().expect("a tracked read");
        assert!(!read.callback_ran, "the VFS runs a read's callback once");
        read.callback_ran = true;
        (
            read.callback.expect("a read with a callback"),
            read.user_data,
        )
    });
    let (ptr, size) = data.unwrap_or((ptr::null_mut(), 0));
    // SAFETY: ptr/size describe a live buffer or the documented no-data form;
    // user_data is the launch's own pointer passed straight back, exactly once.
    let result = unsafe { callback(ptr, size, user_data) };
    with_fake(|f| {
        f.last_callback_size = size;
        f.fail = !result.success;
    });
    if with_fake(|f| f.read.as_ref().is_some_and(|read| read.closed)) {
        with_fake(|f| f.read.take()).expect("the read").release();
    }
}

/// The worker reaching a read launched under hold_callback, after the fact.
pub(super) fn run_held_callback(bytes: Option<&[u8]>) {
    run_read_callback(bytes.map(|bytes| (bytes.as_ptr() as *mut c_void, bytes.len() as i64)));
}

unsafe extern "C" fn fake_mount_open(
    _mount: *mut sys::FlVfsMount,
    _path: sys::RawStr,
    _flags: u32,
) -> u32 {
    with_fake(|f| {
        if f.launch_invalid {
            FL_VFS_HANDLE_INVALID
        } else {
            f.read_pos = 0;
            3
        }
    })
}

/// # Safety
/// buffer must address at least size writable bytes, as the real fl_vfs_read requires.
unsafe extern "C" fn fake_read(_file: u32, buffer: *mut c_void, size: i64) -> u32 {
    with_fake(|f| {
        f.prefix_requested = size;
        let rest = &f.prefix[f.read_pos.min(f.prefix.len())..];
        let count = rest.len().min(size.max(0) as usize);
        // SAFETY: the caller's contract gives size writable bytes at buffer, and count is clamped
        // to that; the fixture's own prefix is the disjoint source.
        unsafe { core::ptr::copy_nonoverlapping(rest.as_ptr(), buffer.cast::<u8>(), count) };
        f.data = rest[..count].to_vec();
        f.read_pos += count;
    });
    4
}

unsafe extern "C" fn fake_get_listing(
    _mount: *mut sys::FlVfsMount,
    _path: sys::RawStr,
    _depth: i32,
) -> u32 {
    if with_fake(|f| f.launch_invalid) {
        FL_VFS_HANDLE_INVALID
    } else {
        2
    }
}

unsafe extern "C" fn fake_is_ready(_handle: u32) -> bool {
    with_fake(|f| {
        if f.mount_closed {
            return false;
        }
        if f.polls_until_ready > 0 {
            f.polls_until_ready -= 1;
            false
        } else {
            true
        }
    })
}

unsafe extern "C" fn fake_wait(_handle: u32) {
    with_fake(|f| {
        f.wait_count += 1;
        f.polls_until_ready = 0;
    });
}

unsafe extern "C" fn fake_get_data(_handle: u32) -> sys::VfsData {
    with_fake(|f| {
        if f.fail {
            sys::VfsData {
                data: core::ptr::null_mut(),
                size: 0,
                success: false,
                error_message: RawStr::EMPTY,
            }
        } else {
            sys::VfsData {
                data: f.data.as_ptr() as *mut u8,
                size: f.data.len() as i64,
                success: true,
                error_message: RawStr::EMPTY,
            }
        }
    })
}

unsafe extern "C" fn fake_get_file_list(_handle: u32) -> sys::VfsFileList {
    // Materialise the C entry array into thread-local storage the returned
    // pointer can name, with each name borrowed from the fake state's own
    // String - so the listing's storage lives exactly as long as the fake
    // state does, and is invalidated by a reset_fake the way a real listing
    // is invalidated by its handle closing.
    FAKE.with(|state| {
        let state = state.borrow();
        FAKE_ENTRIES.with(|storage| {
            let mut storage = storage.borrow_mut();
            *storage = state
                .entries
                .iter()
                .map(|entry| sys::VfsEntry {
                    name: sys::RawStr::borrow(&entry.name),
                    size: entry.size,
                    attributes: entry.attributes,
                    is_directory: entry.is_directory,
                    is_archive: entry.is_archive,
                })
                .collect();
            sys::VfsFileList {
                entries: storage.as_mut_ptr(),
                count: storage.len() as u32,
                success: !state.fail,
                error_message: RawStr::EMPTY,
            }
        })
    })
}

unsafe extern "C" fn fake_close(_handle: u32) {
    with_fake(|f| f.close_count += 1);
    // The tracked read frees at close once its job is done; a close while the
    // callback is still held defers the free to that callback, as the VFS does.
    let done = with_fake(|f| match f.read.as_mut() {
        Some(read) if read.callback_ran => f.read.take(),
        Some(read) => {
            read.closed = true;
            None
        }
        None => None,
    });
    if let Some(read) = done {
        read.release();
    }
}

// needless_update: the struct-update tail gives any slot not listed below the panicking stub.
#[allow(clippy::needless_update)]
pub(super) fn fake_table() -> fake::VfsApi {
    fake::VfsApi {
        fl_vfs_mount_with_options: fake_mount_with_options,
        fl_vfs_mount_close: fake_mount_close,
        fl_vfs_mount_is_ready: fake_mount_is_ready,
        fl_vfs_mount_get_status: fake_mount_get_status,
        fl_vfs_mount_get_info: fake_mount_get_info,
        fl_vfs_mount_read_all_with_options: fake_read_all_with_options,
        fl_vfs_mount_open: fake_mount_open,
        fl_vfs_read: fake_read,
        fl_vfs_get_listing: fake_get_listing,
        fl_vfs_is_ready: fake_is_ready,
        fl_vfs_wait: fake_wait,
        fl_vfs_get_data: fake_get_data,
        fl_vfs_get_file_list: fake_get_file_list,
        fl_vfs_close: fake_close,
        fl_vfs_mount_get_job_handle: fake_mount_get_job_handle,
        fl_vfs_init: fake_init,
        fl_vfs_register_builtin_drivers: fake_register_builtin_drivers,
        ..fake::VfsApi::unimplemented()
    }
}

unsafe extern "C" fn fake_init(arena: *mut sys::Arena) -> bool {
    with_fake(|f| {
        f.init_arena = arena;
        !f.fail
    })
}

unsafe extern "C" fn fake_register_builtin_drivers() {
    with_fake(|f| f.builtin_registrations += 1);
}

unsafe extern "C" fn fake_mount_get_job_handle(_mount: *mut sys::FlVfsMount) -> sys::JobHandle {
    with_fake(|f| sys::JobHandle(f.mount_job_handle))
}

// -----------------------------------------------------------------------
// Owning listings.

pub(super) fn entry(name: &str, is_directory: bool) -> OwnedEntry {
    OwnedEntry {
        name: name.to_string(),
        size: if is_directory { 0 } else { 42 },
        attributes: 0x8000,
        is_directory,
        is_archive: name.ends_with(".zip"),
    }
}
