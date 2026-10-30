//! The VFS slot list: every C entry point this crate calls, named once with its
//! signature.
//!
//! [`vfs_slots!`] is an X-macro: it takes the name of another macro and hands it the
//! whole list. The two [`super::ffi`] forward sets and the fake's field struct, stubs
//! and `unimplemented()` are generated from it; `fake_table()` is written by hand.
//!
//! Each entry is `name(arg: Type, ...) -> Ret;`, with the return arrow omitted for a
//! slot that returns nothing. The argument names are load-bearing: the forwarding
//! macros bind and pass them.

macro_rules! vfs_slots {
    ($emit:ident) => {
        $emit! {
            fl_vfs_init(arena: *mut sys::Arena) -> bool;
            fl_vfs_register_builtin_drivers();
            fl_vfs_mount_with_options(path: sys::RawStr, options: sys::VfsMountOptions) -> sys::VfsMountResult;
            fl_vfs_mount_close(mount: *mut sys::FlVfsMount);
            fl_vfs_mount_get_job_handle(mount: *mut sys::FlVfsMount) -> sys::JobHandle;
            fl_vfs_mount_is_ready(mount: *mut sys::FlVfsMount) -> bool;
            fl_vfs_mount_get_status(mount: *mut sys::FlVfsMount) -> sys::VfsMountStatus;
            fl_vfs_mount_get_info(mount: *mut sys::FlVfsMount) -> sys::VfsMountInfo;
            fl_vfs_mount_read_all_with_options(
                mount: *mut sys::FlVfsMount,
                path: sys::RawStr,
                callback: sys::VfsReadCallback,
                user_data: *mut c_void,
                release: sys::VfsReleaseCallback,
                reuse: u32
            ) -> u32;
            fl_vfs_mount_open(mount: *mut sys::FlVfsMount, path: sys::RawStr, flags: u32) -> u32;
            fl_vfs_read(ticket: u32, buffer: *mut c_void, size: i64) -> u32;
            fl_vfs_get_listing(mount: *mut sys::FlVfsMount, path: sys::RawStr, depth: i32) -> u32;
            fl_vfs_is_ready(ticket: u32) -> bool;
            fl_vfs_wait(ticket: u32);
            fl_vfs_get_data(ticket: u32) -> sys::VfsData;
            fl_vfs_get_file_list(ticket: u32) -> sys::VfsFileList;
            fl_vfs_close(ticket: u32);
        }
    };
}

pub(crate) use vfs_slots;
