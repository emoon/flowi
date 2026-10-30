//! `RawStr` construction and the arena packers.

use super::{fl_arena_create, fl_arena_destroy, Arena, RawStr};

const RESERVE: u64 = 1 << 16;

/// A live arena for one test, destroyed on drop.
struct TestArena(*mut Arena);

impl TestArena {
    fn new() -> TestArena {
        // SAFETY: fl_arena_create takes a reservation size, a NUL-terminated name and a flags word.
        TestArena(unsafe { fl_arena_create(RESERVE, c"rawstr_test".as_ptr(), 0) })
    }
}

impl Drop for TestArena {
    fn drop(&mut self) {
        // SAFETY: self.0 came from fl_arena_create and is destroyed exactly once.
        unsafe { fl_arena_destroy(self.0) }
    }
}

/// The bytes behind an arena-packed RawStr, terminator excluded.
fn packed_bytes(s: RawStr) -> Vec<u8> {
    // SAFETY: the callers below just packed s into a live arena.
    unsafe { core::slice::from_raw_parts(s.data as *const u8, s.len()).to_vec() }
}

#[test]
fn static_literal_packs_in_const_context() {
    const NAME: RawStr = RawStr::from_static("my.plugin");
    assert!(NAME.is_static());
    assert!(NAME.is_ascii());
    assert_eq!(NAME.into_string(), "my.plugin");

    const UNICODE: RawStr = RawStr::from_static("hej så");
    assert!(!UNICODE.is_ascii());
    assert_eq!(UNICODE.len(), "hej så".len());
}

#[test]
fn borrow_round_trips_through_owned() {
    assert_eq!(
        (RawStr::borrow("host said this")).into_string(),
        "host said this"
    );
}

#[test]
fn empty_sentinel_is_null_and_zero_length() {
    assert!(RawStr::EMPTY.data.is_null());
    assert!(RawStr::EMPTY.is_empty());
    assert_eq!(RawStr::EMPTY.len(), 0);
    assert_eq!(RawStr::EMPTY.into_string(), "");
    assert_eq!(RawStr::borrow("").into_string(), "");
}

/// The flag bits sit above the length, so a string is never mistaken for a longer one.
#[test]
fn length_ignores_the_flag_bits() {
    let all_flags = RawStr::from_parts(core::ptr::null(), 5, true, true);
    assert_eq!(all_flags.len(), 5);
}

#[test]
fn arena_str_copies_the_bytes_and_reports_their_length() {
    let arena = TestArena::new();
    // SAFETY: arena is live for the call.
    let packed = unsafe { RawStr::in_arena(arena.0, "content/disk2.adf") };
    assert_eq!(packed.len(), 17);
    assert_eq!(packed.into_string(), "content/disk2.adf");
}

#[test]
fn arena_str_maps_the_empty_string_to_the_sentinel() {
    let arena = TestArena::new();
    // SAFETY: arena is live for the call.
    let packed = unsafe { RawStr::in_arena(arena.0, "") };
    assert!(packed.data.is_null());
    assert!(packed.is_empty());
    assert!(!packed.is_static());
    assert!(!packed.is_ascii());
}

/// is_ascii is set from the content, not unconditionally.
#[test]
fn arena_str_sets_the_ascii_bit_from_the_content() {
    let arena = TestArena::new();
    // SAFETY: arena is live for both calls.
    let (ascii, wide) = unsafe {
        (
            RawStr::in_arena(arena.0, "plain"),
            RawStr::in_arena(arena.0, "Bjørn"),
        )
    };
    assert!(ascii.is_ascii());
    assert!(!wide.is_ascii());
    // Arena copies are not 'static.
    assert!(!ascii.is_static());
}

/// The terminator is written for the C reader but stays out of the length, so a
/// %S and a %s of the same value agree.
#[test]
fn arena_str_nul_terminates_without_counting_the_terminator() {
    let arena = TestArena::new();
    // SAFETY: arena is live for the call, and the packer wrote len + 1 bytes.
    let packed = unsafe { RawStr::in_arena_nul(arena.0, "hvsc") };
    assert_eq!(packed.len(), 4);
    assert_eq!(packed_bytes(packed), b"hvsc");
    // SAFETY: as above - the byte one past the reported length is the terminator.
    assert_eq!(unsafe { *packed.data.add(4) }, 0);
}

#[test]
fn arena_str_nul_maps_the_empty_string_to_the_sentinel() {
    let arena = TestArena::new();
    // SAFETY: arena is live for the call.
    let packed = unsafe { RawStr::in_arena_nul(arena.0, "") };
    assert!(packed.data.is_null());
    assert!(packed.is_empty());
    assert!(!packed.is_static());
    assert!(!packed.is_ascii());
}
