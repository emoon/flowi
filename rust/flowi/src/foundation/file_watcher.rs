//! Watching a directory for file changes.
//!
//! A host that reloads content when it changes on disk - a theme file, a plugin
//! directory, a media folder - starts a [`FileWatcher`] on that directory, polls
//! [`FileWatcher::has_changes`] each frame (which allocates nothing), and takes the
//! pending batch when there is something to look at.
//!
//! Taking a batch clears it, so every change is reported exactly once. The batch owns
//! its paths, so it can be stashed, queued or returned like any other value; iterating
//! it yields [`FileChange`] views borrowed from it.
//!
//! The watcher is owning - dropping it stops the watch and releases its resources.

use core::marker::PhantomData;

use crate::sys;

use crate::Arena;

/// The "no watcher" sentinel (FL_FILE_WATCHER_INVALID) a failed start returns.
const FL_FILE_WATCHER_INVALID: u32 = 0;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// What a watcher should report. [`Default`] is the common case: this directory
/// only, files only, platform-default buffering.
#[derive(Copy, Clone, Debug)]
pub struct WatchConfig {
    /// Watch subdirectories too.
    pub recursive: bool,
    /// Report changes to files.
    pub watch_files: bool,
    /// Report changes to directories.
    pub watch_directories: bool,
    /// Maximum changes to buffer between polls (0 = platform default).
    pub max_changes: u32,
}

impl Default for WatchConfig {
    fn default() -> WatchConfig {
        WatchConfig {
            recursive: false,
            watch_files: true,
            watch_directories: false,
            max_changes: 0,
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// A live watch on one directory. Stops on drop.
pub struct FileWatcher {
    handle: u32,
    _marker: PhantomData<*const ()>,
}

impl FileWatcher {
    /// Start watching path. [`None`] when the path cannot be watched at all - it
    /// does not exist, or the platform refused the watch.
    pub fn start(path: &str, config: WatchConfig) -> Option<FileWatcher> {
        let config = sys::FileWatcherConfig {
            recursive: config.recursive,
            watch_files: config.watch_files,
            watch_directories: config.watch_directories,
            max_changes: config.max_changes,
        };
        // SAFETY: the borrowed path and config only have to outlive the call.
        let handle = unsafe { sys::fl_file_watcher_start(sys::RawStr::borrow(path), &config) };
        (handle != FL_FILE_WATCHER_INVALID).then_some(FileWatcher {
            handle,
            _marker: PhantomData,
        })
    }

    /// True while the watch is still alive. A watched directory that is deleted out
    /// from under the watcher goes inactive.
    #[inline]
    pub fn is_active(&self) -> bool {
        // SAFETY: a live handle from start.
        unsafe { sys::fl_file_watcher_is_active(self.handle) }
    }

    /// True when changes are pending. Allocates nothing, so this is the per-frame
    /// poll; only call [`take_changes`](Self::take_changes) once it says yes.
    #[inline]
    pub fn has_changes(&self) -> bool {
        // SAFETY: a live handle from start.
        unsafe { sys::fl_file_watcher_has_changes(self.handle) }
    }

    /// Take the pending changes and clear them from the watcher. What comes back owns
    /// its strings and is tied to no allocator.
    pub fn take_changes(&self) -> FileChanges {
        Arena::scratch(|arena| {
            // SAFETY: a live handle; the changes and their strings are allocated
            // from arena, which outlives the copies made below.
            let batch =
                unsafe { sys::fl_file_watcher_take_changes(self.handle, arena.as_raw().cast()) };

            let changes = if batch.changes.is_null() || batch.count == 0 {
                Vec::new()
            } else {
                // SAFETY: count records allocated contiguously in arena above.
                let raw =
                    unsafe { core::slice::from_raw_parts(batch.changes, batch.count as usize) };
                raw.iter()
                    .map(|change| OwnedChange {
                        path: change.path.into_string(),
                        old_path: change.old_path.into_string(),
                        change_types: change.change_types,
                        is_directory: change.is_directory,
                        timestamp: change.timestamp,
                    })
                    .collect()
            };

            FileChanges {
                changes,
                has_overflow: batch.has_overflow,
            }
        })
    }
}

impl Drop for FileWatcher {
    fn drop(&mut self) {
        // SAFETY: a live handle from start, stopped exactly once.
        unsafe { sys::fl_file_watcher_stop(self.handle) };
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// One change record, as owned values - the storage [`FileChanges`] keeps. The
/// borrowing view over it is [`FileChange`].
#[derive(Clone, PartialEq, Eq, Debug)]
struct OwnedChange {
    path: String,
    old_path: String,
    change_types: sys::FileChangeType,
    is_directory: bool,
    timestamp: u64,
}

/// One batch of changes taken from a watcher, owning its records. Iterate it to see
/// what changed.
#[derive(Clone, PartialEq, Eq)]
pub struct FileChanges {
    changes: Vec<OwnedChange>,
    has_overflow: bool,
}

impl FileChanges {
    /// True when the watcher dropped changes because its buffer filled between
    /// polls. A caller that must not miss anything treats this as "reload
    /// everything" rather than acting on the partial batch.
    pub fn has_overflow(&self) -> bool {
        self.has_overflow
    }

    /// How many changes are in the batch.
    pub fn len(&self) -> usize {
        self.changes.len()
    }

    /// True when the batch is empty. Note that an empty batch can still report
    /// [`has_overflow`](Self::has_overflow).
    pub fn is_empty(&self) -> bool {
        self.changes.is_empty()
    }

    /// The changes, oldest first.
    pub fn iter(&self) -> impl Iterator<Item = FileChange<'_>> + '_ {
        self.changes.iter().map(|raw| FileChange { raw })
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// One file system change, borrowed from the [`FileChanges`] batch that owns it.
#[derive(Copy, Clone)]
pub struct FileChange<'a> {
    raw: &'a OwnedChange,
}

impl<'a> FileChange<'a> {
    /// Full path to the changed file or directory.
    pub fn path(&self) -> &'a str {
        &self.raw.path
    }

    /// The previous path for a rename or move; empty otherwise.
    pub fn old_path(&self) -> &'a str {
        &self.raw.old_path
    }

    /// What happened to it - a bitmask, since one poll can coalesce several kinds.
    pub fn change_types(&self) -> sys::FileChangeType {
        self.raw.change_types
    }

    /// True when the change affects a directory rather than a file.
    pub fn is_directory(&self) -> bool {
        self.raw.is_directory
    }

    /// When the change occurred, in microseconds since the epoch.
    pub fn timestamp(&self) -> u64 {
        self.raw.timestamp
    }
}

impl core::fmt::Debug for FileChange<'_> {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.debug_struct("FileChange")
            .field("path", &self.path())
            .field("old_path", &self.old_path())
            .field("change_types", &self.change_types())
            .field("is_directory", &self.is_directory())
            .field("timestamp", &self.timestamp())
            .finish()
    }
}

impl core::fmt::Debug for FileChanges {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.debug_struct("FileChanges")
            .field("changes", &self.iter().collect::<Vec<_>>())
            .field("has_overflow", &self.has_overflow)
            .finish()
    }
}
