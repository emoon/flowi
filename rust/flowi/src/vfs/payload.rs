//! The owned results a completed operation hands back: a file's bytes, and a
//! directory listing with a borrowing view over its entries.

use super::*;

/// The bytes of a completed file read - an owned copy, valid after the handle is dropped.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct VfsData {
    bytes: Vec<u8>,
}

impl VfsData {
    /// The file contents.
    #[inline]
    pub fn as_slice(&self) -> &[u8] {
        &self.bytes
    }

    /// Length in bytes.
    #[inline]
    pub fn len(&self) -> usize {
        self.bytes.len()
    }

    /// Whether the file was empty.
    #[inline]
    pub fn is_empty(&self) -> bool {
        self.bytes.is_empty()
    }

    /// Take ownership of the contents.
    #[inline]
    pub fn into_bytes(self) -> Vec<u8> {
        self.bytes
    }
}

impl Payload for VfsData {
    fn drain(ticket: u32) -> Result<VfsData, FlError> {
        // SAFETY: ticket is the live, ready ticket (Payload contract).
        let data = unsafe { ffi::fl_vfs_get_data(ticket) };
        if !data.success {
            // The read result carries no status code, so a failure maps to the generic error.
            return Err(FlError::GenericError);
        }
        let bytes = if data.data.is_null() || data.size <= 0 {
            Vec::new()
        } else {
            // SAFETY: on success the C side guarantees size valid bytes at data, copied
            // out here before fl_vfs_close frees the buffer.
            unsafe { std::slice::from_raw_parts(data.data, data.size as usize) }.to_vec()
        };
        Ok(VfsData { bytes })
    }
}

/// One entry of a completed listing, as owned values - the storage [`FileList`] keeps.
/// The borrowing view over it is [`Entry`].
#[derive(Clone, PartialEq, Eq, Debug)]
pub(super) struct OwnedEntry {
    pub(super) name: String,
    pub(super) size: i64,
    pub(super) attributes: u32,
    pub(super) is_directory: bool,
    pub(super) is_archive: bool,
}

/// A completed directory listing, owning its entries.
///
/// Valid after the handle that produced it is closed. Iterate it for [`Entry`] views.
#[derive(Clone, PartialEq, Eq, Debug)]
pub struct FileList {
    entries: Vec<OwnedEntry>,
}

impl FileList {
    /// How many entries the listing held.
    #[inline]
    pub fn len(&self) -> usize {
        self.entries.len()
    }

    /// Whether the listed directory was empty. An empty directory is not an error.
    #[inline]
    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }

    /// The entries, in listing order.
    pub fn iter(&self) -> impl Iterator<Item = Entry<'_>> + '_ {
        self.entries.iter().map(|raw| Entry { raw })
    }
}

/// One listing entry, borrowed from the [`FileList`] that owns it.
#[derive(Copy, Clone)]
pub struct Entry<'a> {
    raw: &'a OwnedEntry,
}

impl<'a> Entry<'a> {
    /// The entry's name - not a full path.
    #[inline]
    pub fn name(&self) -> &'a str {
        &self.raw.name
    }

    /// Size in bytes; 0 for a directory.
    #[inline]
    pub fn size(&self) -> i64 {
        self.raw.size
    }

    /// Platform-specific attribute bits, as the driver reported them.
    #[inline]
    pub fn attributes(&self) -> u32 {
        self.raw.attributes
    }

    /// Whether the entry is a directory.
    #[inline]
    pub fn is_directory(&self) -> bool {
        self.raw.is_directory
    }

    /// Whether the entry can itself be mounted as an archive (a .zip, say).
    #[inline]
    pub fn is_archive(&self) -> bool {
        self.raw.is_archive
    }
}

impl core::fmt::Debug for Entry<'_> {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.debug_struct("Entry")
            .field("name", &self.name())
            .field("size", &self.size())
            .field("attributes", &self.attributes())
            .field("is_directory", &self.is_directory())
            .field("is_archive", &self.is_archive())
            .finish()
    }
}

impl Payload for FileList {
    fn drain(ticket: u32) -> Result<FileList, FlError> {
        // SAFETY: as VfsData::drain.
        let list = unsafe { ffi::fl_vfs_get_file_list(ticket) };
        if !list.success {
            return Err(FlError::GenericError);
        }
        let entries = if list.entries.is_null() || list.count == 0 {
            Vec::new()
        } else {
            // SAFETY: on success the C side guarantees count entries at entries,
            // copied out here before fl_vfs_close frees the listing's storage.
            let raw = unsafe { std::slice::from_raw_parts(list.entries, list.count as usize) };
            raw.iter()
                .map(|entry| OwnedEntry {
                    name: entry.name.into_string(),
                    size: entry.size,
                    attributes: entry.attributes,
                    is_directory: entry.is_directory,
                    is_archive: entry.is_archive,
                })
                .collect()
        };
        Ok(FileList { entries })
    }
}
