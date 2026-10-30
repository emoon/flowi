//! Pins `RawStr`'s bit packing against the C `FlString` bitfield layout. The C side is
//! `tests/probes/flstring_abi_probe.c`, compiled by build.rs and linked into this test
//! binary: Rust-built strings are decomposed by C, C-built strings
//! are decomposed by Rust.

use std::ffi::{c_char, c_int};

use super::RawStr;

extern "C" {
    #[link_name = "flstring_probe_length"]
    fn probe_length(s: RawStr) -> u64;
    #[link_name = "flstring_probe_is_static"]
    fn probe_is_static(s: RawStr) -> c_int;
    #[link_name = "flstring_probe_is_ascii"]
    fn probe_is_ascii(s: RawStr) -> c_int;
    #[link_name = "flstring_probe_make"]
    fn probe_make(data: *const c_char, length: u64, is_static: c_int, is_ascii: c_int) -> RawStr;
}

struct Probe {
    length: fn(RawStr) -> u64,
    is_static: fn(RawStr) -> c_int,
    is_ascii: fn(RawStr) -> c_int,
    make: fn(*const c_char, u64, c_int, c_int) -> RawStr,
}

fn length(s: RawStr) -> u64 {
    // SAFETY: the probe takes FlString by value and reads only its packed word.
    unsafe { probe_length(s) }
}

fn is_static(s: RawStr) -> c_int {
    // SAFETY: the probe takes FlString by value and reads only its packed word.
    unsafe { probe_is_static(s) }
}

fn is_ascii(s: RawStr) -> c_int {
    // SAFETY: the probe takes FlString by value and reads only its packed word.
    unsafe { probe_is_ascii(s) }
}

fn make(data: *const c_char, len: u64, is_static: c_int, is_ascii: c_int) -> RawStr {
    // SAFETY: the probe only stores the pointer in the returned value and never dereferences it.
    unsafe { probe_make(data, len, is_static, is_ascii) }
}

const PROBE: Probe = Probe {
    length,
    is_static,
    is_ascii,
    make,
};

fn load_probe() -> &'static Probe {
    &PROBE
}

/// Every length boundary crossed with every flag combination.
fn cases() -> Vec<(u64, bool, bool)> {
    let lengths = [
        0,
        1,
        5,
        0xFFFF_FFFF,
        1 << 61,
        (1 << 62) - 2,
        RawStr::MAX_LEN,
    ];
    let mut out = Vec::new();
    for len in lengths {
        for is_static in [false, true] {
            for is_ascii in [false, true] {
                out.push((len, is_static, is_ascii));
            }
        }
    }
    out
}

#[test]
fn rust_built_strings_decompose_in_c() {
    let probe = load_probe();
    for (len, is_static, is_ascii) in cases() {
        let s = RawStr::from_parts(std::ptr::null(), len, is_static, is_ascii);
        assert_eq!(
            (probe.length)(s),
            len,
            "length for {len} {is_static} {is_ascii}"
        );
        assert_eq!(
            (probe.is_static)(s) != 0,
            is_static,
            "is_static for {len} {is_static} {is_ascii}"
        );
        assert_eq!(
            (probe.is_ascii)(s) != 0,
            is_ascii,
            "is_ascii for {len} {is_static} {is_ascii}"
        );
    }
}

#[test]
fn c_built_strings_decompose_in_rust() {
    let probe = load_probe();
    for (len, is_static, is_ascii) in cases() {
        let s = (probe.make)(std::ptr::null(), len, is_static as c_int, is_ascii as c_int);
        assert_eq!(
            s.len() as u64,
            len,
            "length for {len} {is_static} {is_ascii}"
        );
        assert_eq!(
            s.is_static(),
            is_static,
            "is_static for {len} {is_static} {is_ascii}"
        );
        assert_eq!(
            s.is_ascii(),
            is_ascii,
            "is_ascii for {len} {is_static} {is_ascii}"
        );
        let rebuilt = RawStr::from_parts(std::ptr::null(), len, is_static, is_ascii);
        assert_eq!(
            s.packed, rebuilt.packed,
            "packed word for {len} {is_static} {is_ascii}"
        );
    }
}

#[test]
fn constructors_set_the_flags_c_reads() {
    let probe = load_probe();
    let literal = RawStr::from_static("plain");
    assert_eq!((probe.length)(literal), 5);
    assert!((probe.is_static)(literal) != 0 && (probe.is_ascii)(literal) != 0);

    let borrowed = RawStr::borrow("h\u{e9}llo");
    assert_eq!((probe.length)(borrowed), 6);
    assert!((probe.is_static)(borrowed) == 0 && (probe.is_ascii)(borrowed) == 0);
}
