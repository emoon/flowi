//! Slider widgets ([`Slider`]) and their per-call [`SliderConfig`].
//!
//! SliderConfig::format is a printf template validated against the slider's value type before
//! it reaches the native formatter; an invalid one is replaced by the per-type default.

use std::os::raw::c_char;

use crate::sys;

use crate::frame_thread::frame_thread_guard;
use crate::{Signal, WidgetConfig};

/// Per-call configuration for [`Slider::float`] / [`Slider::int`] (and the [`slider_float_with`] /
/// [`slider_int_with`] sugar over them). Build one with
/// ..Default::default() and set only what you need; [`vertical`](Self::vertical) +
/// [`logarithmic`](Self::logarithmic) together give a vertical logarithmic fader. [`Default`]: an empty [`format`](Self::format) selects the per-type default ("%.2f" for float, "%d" for int)
/// and every other field is zero ([`step`](Self::step) 0.0 → auto, 1% of range).
#[derive(Copy, Clone)]
pub struct SliderConfig<'a> {
    /// printf format for the value readout; "" uses the per-type default ("%.2f" float, "%d" int).
    /// Must be literal text plus exactly one conversion matching the slider's type (%f/%.2f/%g etc. for
    /// float, %d/%03d for int; %% is a literal percent). A format with the wrong or extra conversions -
    /// or hazards like %s/%n - is silently ignored and the per-type default used instead.
    pub format: &'a str,
    /// Keyboard / mouse-wheel step; 0.0 auto-calculates 1% of the range.
    pub step: f32,
    /// Logarithmic taper - the volume-fader curve (linear when false).
    pub logarithmic: bool,
    /// Vertical orientation - up/down adjust the value, left/right navigate away (horizontal when false).
    pub vertical: bool,
    /// Track bar thickness in pixels; 0.0 keeps the default 8. A track thinner than
    /// [`thumb_size`](Self::thumb_size) centers vertically against the thumb, so the row stays the thumb's
    /// height either way.
    pub track_height: f32,
    /// Thumb diameter in pixels; 0.0 keeps the default 24. Also the track row's height, and the span the
    /// thumb travels short of so it reaches neither edge.
    pub thumb_size: f32,
    /// Shared widget config - id override, explicit colours, variant/size token, focus and tab order,
    /// per-widget theme override.
    pub widget: WidgetConfig<'a>,
}

impl Default for SliderConfig<'_> {
    fn default() -> Self {
        SliderConfig {
            format: "",
            step: 0.0,
            logarithmic: false,
            vertical: false,
            track_height: 0.0,
            thumb_size: 0.0,
            widget: WidgetConfig::default(),
        }
    }
}

impl SliderConfig<'_> {
    /// Lower to the FFI [`sys::SliderConfig`]. fmt is the sanitized NUL-terminated copy of
    /// [`format`](Self::format) from [`slider_format_cstring`], and must outlive the slider call. The
    /// widget half copies its strings into the per-frame arena, so this must run inside a frame callback.
    fn to_sys(self, fmt: &std::ffi::CStr) -> sys::SliderConfig {
        sys::SliderConfig {
            format: slider_format_ptr(fmt),
            step: self.step,
            logarithmic: self.logarithmic,
            vertical: self.vertical,
            track_height: self.track_height,
            thumb_size: self.thumb_size,
            widget: self.widget.to_sys(),
        }
    }
}

/// The format pointer for a sanitized template: the string itself, or null when empty. Only null
/// selects the per-type default; a pointer to "" renders a blank readout.
fn slider_format_ptr(fmt: &std::ffi::CStr) -> *const c_char {
    if fmt.is_empty() {
        core::ptr::null()
    } else {
        fmt.as_ptr()
    }
}

/// Slider widgets. Each method emits one slider for the current frame and returns its interaction
/// [`Signal`]; call them inside a frame callback.
pub struct Slider;

impl Slider {
    /// Emit a float slider for label bound to `*value` in `[min, max]` and return its interaction
    /// [`Signal`]. config selects the value-readout format, adjustment step, taper, orientation, and the
    /// shared widget settings. A [`format`](SliderConfig::format) that is not literal text plus exactly one
    /// float conversion is ignored and the per-type default ("%.2f") used instead.
    ///
    /// # Panics
    /// Panics when called off the thread that owns the live [`Application`](crate::Application).
    #[inline]
    pub fn float(
        label: impl AsRef<str>,
        value: &mut f32,
        min: f32,
        max: f32,
        config: SliderConfig,
    ) -> Signal {
        let label = label.as_ref();
        let fmt = slider_format_cstring(config.format, false);
        let _frame = frame_thread_guard("Slider::float");
        // SAFETY: fl_frame_str copies the bytes into the frame arena before returning, so the borrow of
        // label only has to outlive the call; the copy stays valid for the rest of the frame. It asserts an
        // active frame, which this slot is emitted inside, and _frame keeps the call to the thread that
        // owns the frame context.
        unsafe {
            sys::fl_slider_float_impl(
                sys::fl_frame_str(label.as_ptr() as *const c_char, label.len()),
                value as *mut f32,
                min,
                max,
                config.to_sys(&fmt),
            )
        }
    }

    /// Emit an integer slider for label bound to `*value` in `[min, max]` and return its interaction
    /// [`Signal`]. config behaves as for [`Slider::float`], except that a valid
    /// [`format`](SliderConfig::format) carries an integer conversion (the per-type default is "%d").
    ///
    /// # Panics
    /// Panics when called off the thread that owns the live [`Application`](crate::Application).
    #[inline]
    pub fn int(
        label: impl AsRef<str>,
        value: &mut i32,
        min: i32,
        max: i32,
        config: SliderConfig,
    ) -> Signal {
        let label = label.as_ref();
        let fmt = slider_format_cstring(config.format, true);
        let _frame = frame_thread_guard("Slider::int");
        // SAFETY: fl_frame_str copies the bytes into the frame arena before returning, so the borrow of
        // label only has to outlive the call; the copy stays valid for the rest of the frame. It asserts an
        // active frame, which this slot is emitted inside, and _frame keeps the call to the thread that
        // owns the frame context.
        unsafe {
            sys::fl_slider_int_impl(
                sys::fl_frame_str(label.as_ptr() as *const c_char, label.len()),
                value as *mut i32,
                min,
                max,
                config.to_sys(&fmt),
            )
        }
    }
}

/// Emit a float slider for label bound to a caller &mut, default config - sugar over
/// [`Slider::float`]. The bound value is read and written in place each frame (drag, wheel, and
/// keyboard all flow back through the pointer). Like [`Button::new`](crate::Button::new), label accepts any `impl AsRef<str>`
/// copied into the per-frame arena; call inside a frame callback. For a fader - a vertical,
/// logarithmically-tapered slider - use [`slider_float_with`] with a [`SliderConfig`] setting vertical and
/// logarithmic.
pub fn slider_float(label: impl AsRef<str>, value: &mut f32, min: f32, max: f32) -> Signal {
    slider_float_with(label, value, min, max, SliderConfig::default())
}

/// Accept only literal text plus exactly one conversion matching the slider's value type: % + optional
/// flags (`- + 0 #` / space) + optional decimal width + optional . decimal precision (each at most 512) +
/// a conversion char from eEfFgG (float) or di (int); %% is literal and does not count. Empty is valid.
/// Everything else is rejected, including an interior NUL byte.
fn valid_slider_format(fmt: &str, is_int: bool) -> bool {
    let bytes = fmt.as_bytes();
    if bytes.contains(&0) {
        return false;
    }
    let mut i = 0;
    let mut conversions = 0;
    while i < bytes.len() {
        if bytes[i] != b'%' {
            i += 1;
            continue;
        }
        i += 1;
        if i >= bytes.len() {
            return false; // trailing lone '%'
        }
        if bytes[i] == b'%' {
            i += 1; // literal "%%", not a conversion
            continue;
        }
        // Optional flags.
        while i < bytes.len() && matches!(bytes[i], b'-' | b'+' | b'0' | b'#' | b' ') {
            i += 1;
        }
        // Optional width: decimal digits only ('*' is left in place and rejected as a bad conversion char).
        let width_start = i;
        while i < bytes.len() && bytes[i].is_ascii_digit() {
            i += 1;
        }
        if i > width_start && fmt[width_start..i].parse::<u32>().map_or(true, |w| w > 512) {
            return false;
        }
        // Optional precision: '.' then decimal digits only.
        if i < bytes.len() && bytes[i] == b'.' {
            i += 1;
            let prec_start = i;
            while i < bytes.len() && bytes[i].is_ascii_digit() {
                i += 1;
            }
            if i > prec_start && fmt[prec_start..i].parse::<u32>().map_or(true, |p| p > 512) {
                return false;
            }
        }
        // Conversion char, matched to the slider's value type.
        if i >= bytes.len() {
            return false;
        }
        let ok = if is_int {
            matches!(bytes[i], b'd' | b'i')
        } else {
            matches!(bytes[i], b'e' | b'E' | b'f' | b'F' | b'g' | b'G')
        };
        if !ok {
            return false;
        }
        i += 1;
        conversions += 1;
        if conversions > 1 {
            return false;
        }
    }
    conversions == 1 || fmt.is_empty()
}

/// A valid template as its NUL-terminated copy. Anything [`valid_slider_format`] rejects silently comes
/// back empty, which selects the per-type default ("%.2f" float, "%d" int).
fn slider_format_cstring(fmt: &str, is_int: bool) -> std::ffi::CString {
    if !valid_slider_format(fmt, is_int) {
        return std::ffi::CString::default();
    }
    // valid_slider_format rejects interior NULs, so the conversion cannot fail.
    std::ffi::CString::new(fmt).unwrap_or_default()
}

/// Emit a float slider with an explicit [`SliderConfig`] - the path to a vertical logarithmic fader. See
/// [`slider_float`] for the binding contract. An invalid [`format`](SliderConfig::format) is silently
/// replaced by the per-type default, as for [`Slider::float`].
pub fn slider_float_with(
    label: impl AsRef<str>,
    value: &mut f32,
    min: f32,
    max: f32,
    config: SliderConfig,
) -> Signal {
    Slider::float(label, value, min, max, config)
}

/// Emit an integer slider for label bound to value, range `[min, max]`, default config. See
/// [`slider_float`] for the binding contract.
pub fn slider_int(label: impl AsRef<str>, value: &mut i32, min: i32, max: i32) -> Signal {
    slider_int_with(label, value, min, max, SliderConfig::default())
}

/// Emit an integer slider with an explicit [`SliderConfig`]. See [`slider_float`] for the binding contract.
/// An invalid [`format`](SliderConfig::format) is silently replaced by the per-type default, as for
/// [`Slider::int`].
pub fn slider_int_with(
    label: impl AsRef<str>,
    value: &mut i32,
    min: i32,
    max: i32,
    config: SliderConfig,
) -> Signal {
    Slider::int(label, value, min, max, config)
}

#[cfg(test)]
mod tests {
    use super::{slider_format_cstring, slider_format_ptr, valid_slider_format, SliderConfig};

    #[test]
    fn accepts_valid_float_formats() {
        for fmt in [
            "", "%.2f", "%5.1f", "%g", "%.0f dB", "%e", "%E", "%F", "%G", "%+08.3f",
        ] {
            assert!(
                valid_slider_format(fmt, false),
                "float should accept {fmt:?}"
            );
        }
    }

    #[test]
    fn accepts_valid_int_formats() {
        for fmt in ["", "%d", "%i", "%d%%", "%03d ms", "%-5d"] {
            assert!(valid_slider_format(fmt, true), "int should accept {fmt:?}");
        }
    }

    #[test]
    fn rejects_hazardous_float_formats() {
        // Type mismatches, extra/zero conversions, and the classic UB specifiers.
        for fmt in [
            "%s", "%d", "%d %d", "%n", "%*f", "%.*f", "%1$f", "%lf", "100%", "%", "%x", "no conv",
        ] {
            assert!(
                !valid_slider_format(fmt, false),
                "float should reject {fmt:?}"
            );
        }
    }

    #[test]
    fn rejects_hazardous_int_formats() {
        for fmt in ["%s", "%f", "%d %d", "%n", "%*d", "%1$d", "%ld", "100%", "%"] {
            assert!(!valid_slider_format(fmt, true), "int should reject {fmt:?}");
        }
    }

    #[test]
    fn rejects_oversized_width_and_precision() {
        assert!(!valid_slider_format("%513f", false));
        assert!(!valid_slider_format("%.513f", false));
        assert!(!valid_slider_format("%99999999999f", false));
        assert!(valid_slider_format("%512.512f", false));
    }

    #[test]
    fn rejects_interior_nul_formats() {
        for fmt in ["%.2f\0", "\0%.2f", "%.2\0f"] {
            assert!(
                !valid_slider_format(fmt, false),
                "float should reject {fmt:?}"
            );
        }
        for fmt in ["%d\0", "\0%d", "%\0d"] {
            assert!(!valid_slider_format(fmt, true), "int should reject {fmt:?}");
        }
    }

    #[test]
    fn interior_nul_format_lowers_to_default_null_pointer() {
        // An interior-NUL format lowers exactly like an unset one: to a null pointer.
        let nul_config = SliderConfig {
            format: "%.2f\0",
            ..Default::default()
        };
        let unset_config = SliderConfig::default();

        let nul_fmt = slider_format_cstring(nul_config.format, false);
        let unset_fmt = slider_format_cstring(unset_config.format, false);
        assert!(nul_fmt.is_empty());
        assert_eq!(nul_fmt, unset_fmt);
        assert!(slider_format_ptr(&nul_fmt).is_null());
        assert!(slider_format_ptr(&unset_fmt).is_null());

        let int_fmt = slider_format_cstring("%d\0", true);
        assert!(int_fmt.is_empty());
        assert!(slider_format_ptr(&int_fmt).is_null());
    }

    #[test]
    fn valid_format_passes_through_unchanged() {
        // A valid template crosses byte-for-byte as a non-null pointer.
        let config = SliderConfig {
            format: "%.1f dB",
            ..Default::default()
        };
        let fmt = slider_format_cstring(config.format, false);
        assert_eq!(fmt.to_bytes(), b"%.1f dB");
        assert_eq!(slider_format_ptr(&fmt), fmt.as_ptr());
    }

    #[test]
    fn literal_percent_is_not_a_conversion() {
        // "%%" is a literal percent, so this string has exactly one real conversion.
        assert!(valid_slider_format("%d%% done", true));
        // ...and one made only of literals (no conversion) falls back.
        assert!(!valid_slider_format("100%% done", true));
    }
}
