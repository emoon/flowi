//! Reading files and listing directories through flowi's virtual filesystem.
//!
//! [`Vfs`] is the mount factory; there is one VFS per process, so [`Vfs::mount`] is an
//! associated function: it opens a directory, archive or URL and returns an owning
//! [`OwnedMount`]. Every operation on a mount is a method on [`Mount`], which
//! OwnedMount derefs to.
//!
//! Every read or listing is asynchronous. Two surfaces cover it:
//!
//! - [`Mount`] is the frame-loop surface. [`Mount::read`], [`Mount::listing`] and
//!   [`Mount::read_with`] launch an operation and return an owning [`Handle`] you
//!   poll once per frame. A failed launch is [`None`]; dropping a handle closes the
//!   operation without blocking. See [`Handle`].
//! - [`WorkerMount`] is the job-body surface: a Send view of a mount whose read,
//!   read_prefix and listing block and return the finished value.
//!
//! Both return owned results: [`VfsData`] owns the file's bytes and [`FileList`] owns
//! every entry's name. Iterating a FileList yields [`Entry`] views borrowed from the
//! list.
//!
//! [`Mount`] and the [`Handle`]s it mints are !Send + !Sync; [`WorkerMount`] is the
//! way across.

use core::marker::PhantomData;
use core::ops::Deref;
use core::ptr;
use std::os::raw::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::Mutex;

mod executor;
mod handle;

/// The typed async-handle wrapper.
pub use handle::{Handle, HandleFuture, Payload, Poll};

/// The frame-ticked local executor and its owning task handle.
pub use executor::{LocalExecutor, Task};

use crate::sys;
use crate::sys::FlError;

use crate::sys::RawStr;

/// The C FlVfsHandle - a recycled u32 ticket for one async operation. 0 is the
/// invalid sentinel a failed launch returns; the surface maps it to [`None`].
type FlVfsHandle = u32;
const FL_VFS_HANDLE_INVALID: FlVfsHandle = 0;

/// FlVfsOpenFlags_Read - the one open mode this surface needs, for the bounded
/// prefix read.
const VFS_OPEN_READ: u32 = 1;

#[cfg(test)]
pub(crate) mod fake;
pub(crate) mod ffi;
mod mapped;
mod mount;
mod payload;
mod slots;
#[cfg(test)]
mod tests;

pub use mapped::MappedRead;
pub use mount::{
    Mount, MountError, MountInfo, MountOptions, MountStatus, OwnedMount, Vfs, WorkerMount,
};
pub use payload::{Entry, FileList, VfsData};
