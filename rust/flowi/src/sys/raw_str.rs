//! The Rust mirror of the C `FlString` view.

use std::os::raw::c_char;

use super::{fl_arena_alloc_raw, Arena};

/// Mirror of FlString - a non-owning counted view of bytes crossing the C ABI:
///
/// ```c
/// typedef struct FlString {
///     const char* data;
///     uint64_t length : 62;
///     uint64_t is_static : 1;
///     uint64_t is_ascii : 1;
/// } FlString;
/// ```
///
/// The type does not track how long the bytes live; that comes from the constructor that
/// built it: [`RawStr::from_static`] ('static), [`RawStr::borrow`] (the single call it is
/// passed to), [`RawStr::in_arena`] (the arena's lifetime), or a host return (whatever the
/// host's contract says). Storing one in a struct field is a lifetime obligation the caller
/// tracks by hand.
///
/// The trailing bitfield word is mirrored as a plain u64, assembled only in
/// [`RawStr::from_parts`].
#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct RawStr {
    pub data: *const c_char,
    pub packed: u64,
}

impl RawStr {
    /// The empty sentinel: null data, zero length. The arena constructors return it for "".
    pub const EMPTY: RawStr = RawStr {
        data: core::ptr::null(),
        packed: 0,
    };

    /// The largest length the 62-bit field holds.
    pub const MAX_LEN: u64 = (1u64 << 62) - 1;
    const STATIC_BIT: u32 = 62;
    const ASCII_BIT: u32 = 63;

    /// The one place the bitfield word is assembled: length in the low 62 bits, then
    /// is_static, then is_ascii. len must not exceed [`RawStr::MAX_LEN`].
    pub const fn from_parts(
        data: *const c_char,
        len: u64,
        is_static: bool,
        is_ascii: bool,
    ) -> RawStr {
        assert!(len <= RawStr::MAX_LEN, "RawStr length exceeds 62 bits");
        RawStr {
            data,
            packed: len
                | ((is_static as u64) << RawStr::STATIC_BIT)
                | ((is_ascii as u64) << RawStr::ASCII_BIT),
        }
    }

    /// Pack a 'static Rust string (is_static = 1). is_ascii is set only when the bytes
    /// are ASCII.
    ///
    /// Usable in a const item:
    ///
    /// ```ignore
    /// const NAME: RawStr = RawStr::from_static("my.plugin");
    /// ```
    pub const fn from_static(s: &'static str) -> RawStr {
        RawStr::from_parts(
            s.as_ptr() as *const c_char,
            s.len() as u64,
            true,
            s.is_ascii(),
        )
    }

    /// Borrow a &str for the duration of a single call (is_static = 0): the view aliases
    /// s's bytes and is valid only while s outlives the one FFI call it is handed to.
    pub fn borrow(s: &str) -> RawStr {
        RawStr::borrow_bytes(s.as_bytes())
    }

    /// [`RawStr::borrow`] for a byte string that is not known to be UTF-8.
    pub fn borrow_bytes(s: &[u8]) -> RawStr {
        RawStr::from_parts(
            s.as_ptr() as *const c_char,
            s.len() as u64,
            false,
            s.is_ascii(),
        )
    }

    /// The is_static flag: the bytes are static/literal storage.
    pub const fn is_static(self) -> bool {
        (self.packed >> RawStr::STATIC_BIT) & 1 != 0
    }

    /// The is_ascii flag: the bytes are known to be ASCII-only.
    pub const fn is_ascii(self) -> bool {
        (self.packed >> RawStr::ASCII_BIT) & 1 != 0
    }

    /// The number of bytes the view carries.
    pub const fn len(self) -> usize {
        (self.packed & RawStr::MAX_LEN) as usize
    }

    /// Whether the view carries no bytes. A null view and a zero-length one both count.
    pub const fn is_empty(self) -> bool {
        self.len() == 0
    }

    /// Copy a host-returned view into an owned String. A null data or zero length yields
    /// the empty string; non-UTF-8 bytes are replaced (lossy). Never panics.
    ///
    /// The host contract is that data..data + length is initialised and outlives this copy.
    pub fn into_string(self) -> String {
        if self.data.is_null() || self.is_empty() {
            return String::new();
        }
        // SAFETY: host contract above - data points at len() initialised bytes for the
        // duration of this call.
        let bytes = unsafe { core::slice::from_raw_parts(self.data as *const u8, self.len()) };
        String::from_utf8_lossy(bytes).into_owned()
    }

    /// Borrow a host-returned view as a &str, no copy, for a string the host keeps alive on
    /// the caller's behalf.
    ///
    /// A null data, a zero length, or non-UTF-8 bytes all yield "". Never panics.
    ///
    /// # Safety
    /// data..data + length must be initialised and stay valid and unmodified for the whole
    /// of 'a. The caller picks 'a, so it must be tied to something that actually owns the
    /// bytes (the mount, the arena) - never left to be inferred.
    pub unsafe fn as_str<'a>(&self) -> &'a str {
        if self.data.is_null() || self.is_empty() {
            return "";
        }
        // SAFETY: the caller's contract - data..data + length is initialised and stays valid and unmodified
        // for the whole of 'a; the null/zero cases returned above.
        let bytes = unsafe { core::slice::from_raw_parts(self.data as *const u8, self.len()) };
        core::str::from_utf8(bytes).unwrap_or("")
    }

    /// Copy s into arena and pack the copy. "" maps to [`RawStr::EMPTY`].
    ///
    /// is_static is 0 and is_ascii reflects the content.
    ///
    /// The bytes are not NUL-terminated: use [`RawStr::in_arena_nul`] wherever the C side
    /// reads them as a C string.
    ///
    /// # Safety
    /// arena must be a live arena for the call, and nothing checks that it is.
    pub unsafe fn in_arena(arena: *mut Arena, s: &str) -> RawStr {
        if s.is_empty() {
            return RawStr::EMPTY;
        }
        // SAFETY: arena is live for the call (the caller's contract), so the allocation returns s.len()
        // writable, uniquely-owned bytes; s is a live &str and the two regions cannot overlap.
        unsafe {
            let buf = fl_arena_alloc_raw(arena, s.len() as u64, 1) as *mut u8;
            core::ptr::copy_nonoverlapping(s.as_ptr(), buf, s.len());
            RawStr::packed_in_arena(buf as *const c_char, s)
        }
    }

    /// Copy s into arena NUL-terminated: the terminator is written but excluded from the
    /// reported length, so data can be read as a C string. "" maps to [`RawStr::EMPTY`].
    ///
    /// # Safety
    /// arena must be a live arena for the call, and nothing checks that it is.
    pub unsafe fn in_arena_nul(arena: *mut Arena, s: &str) -> RawStr {
        if s.is_empty() {
            return RawStr::EMPTY;
        }
        // SAFETY: arena is live for the call (the caller's contract), so the allocation returns
        // s.len() + 1 writable, uniquely-owned bytes; s is a live &str that cannot overlap them, and
        // the terminator is written at the last of the requested bytes - in bounds.
        unsafe {
            let buf = fl_arena_alloc_raw(arena, (s.len() + 1) as u64, 1) as *mut u8;
            core::ptr::copy_nonoverlapping(s.as_ptr(), buf, s.len());
            *buf.add(s.len()) = 0;
            RawStr::packed_in_arena(buf as *const c_char, s)
        }
    }

    /// Pack the (data, len) of a copy just made into an arena.
    fn packed_in_arena(data: *const c_char, s: &str) -> RawStr {
        RawStr::from_parts(data, s.len() as u64, false, s.is_ascii())
    }
}
