//! Path manipulation that follows core's C path rules rather than std::path's.
//!
//! Two rules differ from std::path:
//!
//! * A backslash separates on every platform, Linux included. Separators are not
//!   rewritten: what comes back carries whichever ones went in, so
//!   parent(r"docs\sub\readme.txt") is r"docs\sub". Only the separator [`join`](crate::path::join)
//!   inserts is always /.
//! * A URL is absolute. http:// and https:// count alongside a leading /, a drive
//!   letter and a UNC prefix.
//!
//! Each function copies its result out of a scratch arena, so nothing here borrows
//! from an arena the caller has to keep alive.
//!
//! ```
//! use flowi_core::path;
//!
//! assert_eq!(path::join("/docs", "readme.txt"), "/docs/readme.txt");
//! assert_eq!(path::parent("/docs/readme.txt"), "/docs");
//! assert!(path::is_absolute("https://example.com/x.zip"));
//! ```

use crate::sys;

use crate::Arena;

/// Join relative onto base. An absolute relative replaces base outright rather
/// than being appended to it.
#[inline]
pub fn join(base: &str, relative: &str) -> String {
    Arena::scratch(|arena| {
        // SAFETY: both borrows outlive the call, and the result is copied out of
        // the scratch arena before it is released.
        unsafe {
            sys::fl_path_join(
                arena.as_raw(),
                sys::RawStr::borrow(base),
                sys::RawStr::borrow(relative),
            )
        }
        .into_string()
    })
}

/// The parent directory of path. A bare filename (or an empty path) yields an
/// empty string; a path directly under / (and / itself) yields /.
#[inline]
pub fn parent(path: &str) -> String {
    Arena::scratch(|arena| {
        // SAFETY: the borrow outlives the call, and the result is copied out of
        // the scratch arena before it is released.
        unsafe { sys::fl_path_get_parent(arena.as_raw(), sys::RawStr::borrow(path)) }.into_string()
    })
}

/// Whether path is absolute: a leading /, a Windows drive letter or UNC
/// prefix, or an http:// / https:// URL.
#[inline]
pub fn is_absolute(path: &str) -> bool {
    // SAFETY: the borrow outlives the call, which only reads the bytes.
    unsafe { sys::fl_path_is_absolute(sys::RawStr::borrow(path)) }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn joining_inserts_exactly_one_separator() {
        assert_eq!(join("/docs", "readme.txt"), "/docs/readme.txt");
        assert_eq!(join("/docs/", "readme.txt"), "/docs/readme.txt");
        assert_eq!(join("/docs", "sub/readme.txt"), "/docs/sub/readme.txt");
    }

    #[test]
    fn joining_an_empty_component_yields_the_other() {
        assert_eq!(join("", "readme.txt"), "readme.txt");
        assert_eq!(join("/docs", ""), "/docs");
    }

    #[test]
    fn an_absolute_relative_component_wins_outright() {
        assert_eq!(join("/docs", "/cache/readme.txt"), "/cache/readme.txt");
        assert_eq!(join("/docs", "https://host/x.zip"), "https://host/x.zip");
    }

    #[test]
    fn the_parent_is_everything_before_the_last_separator() {
        assert_eq!(parent("/docs/readme.txt"), "/docs");
        assert_eq!(parent("/docs/sub/readme.txt"), "/docs/sub");
        // A trailing separator is not a component of its own.
        assert_eq!(parent("/docs/sub/"), "/docs");
    }

    #[test]
    fn a_path_without_a_parent_is_empty() {
        assert_eq!(parent("readme.txt"), "");
        assert_eq!(parent(""), "");
    }

    #[test]
    fn a_backslash_separates_but_is_not_rewritten() {
        assert_eq!(parent(r"docs\sub\readme.txt"), r"docs\sub");
        // Only the separator join inserts is a forward slash; the base is
        // reproduced as-is...
        assert_eq!(join(r"docs\sub", "readme.txt"), r"docs\sub/readme.txt");
        // ...and a trailing backslash already counts as one, so none is added.
        assert_eq!(join(r"docs/sub\", "readme.txt"), r"docs/sub\readme.txt");
    }

    #[test]
    fn a_file_at_the_root_has_the_root_as_its_parent() {
        assert_eq!(parent("/readme.txt"), "/");
        assert_eq!(parent("/"), "/");
    }

    #[test]
    fn drive_letters_and_unc_prefixes_are_absolute() {
        assert!(is_absolute(r"C:\docs\readme.txt"));
        assert!(is_absolute(r"\\host\share\readme.txt"));
    }

    #[test]
    fn absoluteness_covers_roots_drives_and_urls() {
        assert!(is_absolute("/docs/readme.txt"));
        assert!(is_absolute("C:/docs/readme.txt"));
        assert!(is_absolute("http://example.com/x.zip"));
        assert!(is_absolute("https://example.com/x.zip"));

        assert!(!is_absolute("docs/readme.txt"));
        assert!(!is_absolute("readme.txt"));
        assert!(!is_absolute(""));
    }
}
