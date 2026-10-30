//! Arguments for a localized message that carries runtime values, plus the [`loc_fmt!`](crate::loc_fmt!) /
//! [`loc_plural!`](crate::loc_plural!) call-site sugar over them.
//!
//! An argument list is a stack array of [`FmtArg`] that crosses the FFI as-is: integers and floats
//! travel as raw i64 / f64 and are formatted by the native library, and a string argument travels as a view of
//! the caller's own bytes. Nothing on this path allocates.

use crate::generated;
use core::marker::PhantomData;
use crate::sys::{FmtArgKind, FmtArgValue};

impl generated::localization::Localization {
    /// Point flowi at the language manifest `manifest_path` inside `mount`, and load the default
    /// language. Call once before the first frame; with `hot_reload` the tables reload when their
    /// files change.
    ///
    /// # Safety
    /// `mount` must outlive the application: the string tables are read through it until the
    /// application tears down.
    pub unsafe fn init(mount: &crate::vfs::Mount, manifest_path: impl AsRef<str>, hot_reload: bool) {
        let manifest_path = manifest_path.as_ref();
        let _frame = crate::frame_thread::frame_thread_guard("Localization::init");
        // SAFETY: the caller keeps the mount alive for the application's lifetime; the manifest path
        // is read during the call only.
        unsafe {
            crate::sys::fl_localization_init(
                mount.as_raw().cast(),
                crate::sys::RawStr::borrow(manifest_path),
                hot_reload,
            )
        };
    }
}

/// One argument for an indexed placeholder. 'a is the borrow a string argument takes out on the
/// caller's bytes; the call the argument is handed to reads them and never keeps them.
///
/// Build one with From - FmtArg::from(3), FmtArg::from(1.5), FmtArg::from(name) - or let
/// [`loc_fmt!`](crate::loc_fmt!) / [`loc_plural!`](crate::loc_plural!) do it per argument.
#[repr(transparent)]
#[derive(Copy, Clone)]
pub struct FmtArg<'a> {
    raw: crate::sys::FmtArg,
    borrowed: PhantomData<&'a str>,
}

// `#[repr(transparent)]` gives FmtArg the layout of crate::sys::FmtArg, which the generated
// definition pins to FlFmtArg's size, alignment and field offsets.
const _: () = {
    assert!(
        core::mem::size_of::<FmtArg<'static>>() == core::mem::size_of::<crate::sys::FmtArg>(),
        "FmtArg size drift"
    );
    assert!(
        core::mem::align_of::<FmtArg<'static>>() == core::mem::align_of::<crate::sys::FmtArg>(),
        "FmtArg align drift"
    );
};

impl<'a> FmtArg<'a> {
    fn new(value: FmtArgValue, kind: FmtArgKind) -> FmtArg<'a> {
        FmtArg {
            raw: crate::sys::FmtArg { value, kind },
            borrowed: PhantomData,
        }
    }
}

/// Integer arguments cross as i64; a u64 or usize above i64::MAX wraps.
macro_rules! fmt_arg_from_int {
    ($($ty:ty),* $(,)?) => {
        $(
            impl<'a> From<$ty> for FmtArg<'a> {
                fn from(value: $ty) -> FmtArg<'a> {
                    FmtArg::new(
                        FmtArgValue {
                            int_value: value as i64,
                        },
                        FmtArgKind::Int,
                    )
                }
            }
        )*
    };
}

fmt_arg_from_int!(i8, i16, i32, i64, isize, u8, u16, u32, u64, usize);

impl<'a> From<f32> for FmtArg<'a> {
    fn from(value: f32) -> FmtArg<'a> {
        FmtArg::new(
            FmtArgValue {
                float_value: f64::from(value),
            },
            FmtArgKind::Float,
        )
    }
}

impl<'a> From<f64> for FmtArg<'a> {
    fn from(value: f64) -> FmtArg<'a> {
        FmtArg::new(FmtArgValue { float_value: value }, FmtArgKind::Float)
    }
}

impl<'a, T: ?Sized + AsRef<str>> From<&'a T> for FmtArg<'a> {
    /// A borrowed view of value - &str, &String, or anything else that lends a &str.
    fn from(value: &'a T) -> FmtArg<'a> {
        FmtArg::new(
            FmtArgValue {
                string_value: crate::sys::RawStr::borrow(value.as_ref()),
            },
            FmtArgKind::Str,
        )
    }
}

fn as_raw<'a>(args: &'a [FmtArg<'_>]) -> &'a [crate::sys::FmtArg] {
    // SAFETY: FmtArg is `#[repr(transparent)]` over crate::sys::FmtArg and adds only a
    // zero-sized PhantomData, so the two slices have the same layout and element count.
    unsafe { core::slice::from_raw_parts(args.as_ptr().cast(), args.len()) }
}

/// Display text that carries runtime values: look the key up in the active language, then
/// substitute the caller's arguments into its indexed placeholders. The result is passed to any
/// widget the way plain text is. [`loc_fmt!`](crate::loc_fmt!) and [`loc_plural!`](crate::loc_plural!) are the call-site sugar over
/// these two.
pub struct Localize;

impl Localize {
    /// Look sid up in the active language and substitute args into its indexed placeholders -
    /// {0} is the first argument, {1} the second, and a repeated index substitutes at every
    /// occurrence. {{ and }} render as a literal brace; a placeholder with no matching argument
    /// renders as itself and warns. Integers and floats format printf-style (. decimal point, no digit
    /// grouping) and string arguments substitute as-is.
    #[inline]
    pub fn fmt(sid: impl AsRef<str>, args: &[FmtArg<'_>]) -> String {
        generated::localization::Localize::fmt(sid, as_raw(args))
    }

    /// Look sid up in the active language, pick the plural variant count selects under the
    /// active language's rules, and substitute into it. count is itself the {0} argument and
    /// args follow it as {1} onwards. A language with no built-in rules always selects other
    /// and warns once. Placeholder and number-formatting rules are [`Localize::fmt`]'s.
    #[inline]
    pub fn plural(sid: impl AsRef<str>, count: i64, args: &[FmtArg<'_>]) -> String {
        generated::localization::Localize::plural(sid, count, as_raw(args))
    }
}

/// Look the key sid up in the active language and substitute the arguments that follow into its
/// indexed placeholders - {0} is the first, {1} the second. Each argument converts through
/// [`FmtArg`]'s From impls, so integers, floats and strings mix freely.
///
/// ```ignore
/// Text::emit(loc_fmt!("SID_greeting", name));
/// ```
#[macro_export]
macro_rules! loc_fmt {
    ($sid:expr $(, $arg:expr)* $(,)?) => {
        $crate::Localize::fmt($sid, &[$($crate::FmtArg::from($arg)),*])
    };
}

/// Look the key sid up, pick the plural variant count selects, and substitute. count is
/// itself {0} and the arguments that follow are {1} onwards.
///
/// ```ignore
/// Text::emit(loc_plural!("SID_file_count", files.len()));
/// ```
#[macro_export]
macro_rules! loc_plural {
    ($sid:expr, $count:expr $(, $arg:expr)* $(,)?) => {
        $crate::Localize::plural($sid, $count as i64, &[$($crate::FmtArg::from($arg)),*])
    };
}

#[cfg(test)]
mod tests {
    use super::*;

    fn int_of(arg: FmtArg<'_>) -> i64 {
        assert!(
            matches!(arg.raw.kind, FmtArgKind::Int),
            "expected an Int arg"
        );
        // SAFETY: the Int kind above is what says int_value is the live arm.
        unsafe { arg.raw.value.int_value }
    }

    fn float_of(arg: FmtArg<'_>) -> f64 {
        assert!(
            matches!(arg.raw.kind, FmtArgKind::Float),
            "expected a Float arg"
        );
        // SAFETY: the Float kind above is what says float_value is the live arm.
        unsafe { arg.raw.value.float_value }
    }

    fn str_of<'a>(arg: FmtArg<'a>) -> &'a str {
        assert!(
            matches!(arg.raw.kind, FmtArgKind::Str),
            "expected a Str arg"
        );
        // SAFETY: the Str kind above is what says string_value is the live arm, and the
        // returned view is tied to the borrow the argument itself carries.
        unsafe {
            let raw = arg.raw.value.string_value;
            core::str::from_utf8(core::slice::from_raw_parts(raw.data.cast(), raw.len()))
                .expect("the borrowed bytes came from a &str")
        }
    }

    #[test]
    fn integers_widen_to_i64() {
        assert_eq!(int_of(FmtArg::from(-3i8)), -3);
        assert_eq!(int_of(FmtArg::from(-3i16)), -3);
        assert_eq!(int_of(FmtArg::from(-3i32)), -3);
        assert_eq!(int_of(FmtArg::from(-3i64)), -3);
        assert_eq!(int_of(FmtArg::from(-3isize)), -3);
        assert_eq!(int_of(FmtArg::from(200u8)), 200);
        assert_eq!(int_of(FmtArg::from(60_000u16)), 60_000);
        assert_eq!(int_of(FmtArg::from(4_000_000_000u32)), 4_000_000_000);
        assert_eq!(int_of(FmtArg::from(7u64)), 7);
        assert_eq!(int_of(FmtArg::from(7usize)), 7);

        assert_eq!(
            int_of(FmtArg::from(u64::MAX)),
            -1,
            "an unsigned value above i64::MAX wraps, as it does through the C sugar's cast"
        );
    }

    #[test]
    fn floats_widen_to_f64() {
        assert_eq!(float_of(FmtArg::from(1.5f32)), 1.5);
        assert_eq!(float_of(FmtArg::from(-0.25f64)), -0.25);
    }

    #[test]
    fn strings_are_borrowed_views() {
        let owned = String::from("Ada");
        let borrowed = FmtArg::from(&owned);
        assert_eq!(str_of(borrowed), "Ada");
        // SAFETY: Str is the live arm, checked by str_of above.
        let data = unsafe { borrowed.raw.value.string_value.data };
        assert_eq!(
            data.cast::<u8>(),
            owned.as_ptr(),
            "the argument must alias the caller's bytes, not a copy"
        );

        assert_eq!(str_of(FmtArg::from("literal")), "literal");
    }

    #[test]
    fn a_mixed_argument_list_is_a_plain_stack_array() {
        let name = String::from("Ada");
        let args = [
            FmtArg::from(2u32),
            FmtArg::from(0.5f32),
            FmtArg::from(&name),
        ];

        assert_eq!(int_of(args[0]), 2);
        assert_eq!(float_of(args[1]), 0.5);
        assert_eq!(str_of(args[2]), "Ada");
    }
}
