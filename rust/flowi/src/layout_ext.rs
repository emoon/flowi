//! Layout containers with a hand-built config.
//!
//! The `col!` / `row!` macros cover most of what a UI draws; a container they
//! cannot express — asymmetric padding, a config built up conditionally —
//! reaches for the raw [`Layout::start`] / [`Layout::configure`] /
//! [`Layout::end`] trio instead. The container helpers here wrap that trio in
//! a closure whose end call runs on drop, so an early return or unwind still
//! closes the container.
//!
//! The config builders each name only the one or two fields that vary,
//! leaving the rest at their zero default.

use crate::{
    Color, CornerRadius, DropdownConfig, Layout, LayoutConfig, LayoutEndGuard, LayoutId,
    LayoutPadding, TextConfig, TextVAlign, WidgetConfig,
};

/// Run `body` inside a layout container identified by `id`.
///
/// The container closes when `body` returns, including on an early return or an
/// unwind.
pub fn container<R>(id: impl Into<LayoutId>, config: LayoutConfig, body: impl FnOnce() -> R) -> R {
    Layout::start(id);
    Layout::configure(config);
    let _end = LayoutEndGuard::new();
    body()
}

/// Run `body` inside an anonymous layout container, for the containers that are
/// pure structure and never need to be looked up by id.
pub fn container_auto<R>(config: LayoutConfig, body: impl FnOnce() -> R) -> R {
    Layout::start_auto();
    Layout::configure(config);
    let _end = LayoutEndGuard::new();
    body()
}

/// Padding given per edge, in left, right, top, bottom order — the struct's own
/// field order, not CSS's clockwise one.
#[inline]
pub fn padding(left: u16, right: u16, top: u16, bottom: u16) -> LayoutPadding {
    LayoutPadding {
        left,
        right,
        top,
        bottom,
    }
}

/// Padding on the left and right edges only — `LayoutPadding::from((0, amount))`.
#[inline]
pub fn padding_h(amount: u16) -> LayoutPadding {
    LayoutPadding::from((0, amount))
}

/// The same padding on all four edges — `LayoutPadding::from(amount)`, named for
/// the raw-config call sites.
#[inline]
pub fn padding_all(amount: u16) -> LayoutPadding {
    LayoutPadding::from(amount)
}

/// Separate horizontal and vertical padding —
/// `LayoutPadding::from((vertical, horizontal))` with the arguments in the
/// opposite, horizontal-first order.
#[inline]
pub fn padding_hv(horizontal: u16, vertical: u16) -> LayoutPadding {
    LayoutPadding::from((vertical, horizontal))
}

/// Zero padding on every edge.
#[inline]
pub fn padding_none() -> LayoutPadding {
    LayoutPadding::from(0)
}

/// Styled text in `color` at `size`, with every other field at its default.
/// `valign` is `Baseline`, not centred.
///
/// A `size` of zero selects the font's own size.
pub fn text_config(color: Color, size: u16) -> TextConfig {
    TextConfig {
        font_size: size,
        color,
        valign: TextVAlign::Baseline,
        ..TextConfig::default()
    }
}

/// The zeroed widget config with its id overridden.
///
/// Widgets whose label is empty, or which appear more than once in a frame,
/// need an explicit id — without one they would derive the same id from the
/// same empty label and collide.
///
/// The id is borrowed for the duration of the widget call, so a literal or any
/// string that lives across the call will do.
pub fn widget_config(id_override: &str) -> WidgetConfig<'_> {
    WidgetConfig {
        id_override,
        ..WidgetConfig::default()
    }
}

/// A dropdown with zero defaults, its popup width, and the caller's
/// widget config.
///
/// `min_width` of zero takes flowi's own default width.
pub fn dropdown_config(min_width: f32, widget: WidgetConfig<'_>) -> DropdownConfig<'_> {
    DropdownConfig {
        min_width,
        widget,
        ..DropdownConfig::default()
    }
}

/// The same radius on all four corners — [`CornerRadius::uniform`], named to
/// pair with the top/bottom variants below.
#[inline]
pub fn corner_radius(value: f32) -> CornerRadius {
    CornerRadius::uniform(value)
}

/// Rounding on the top two corners only — the shape a bar that caps a panel
/// takes.
#[inline]
pub fn corner_radius_top(value: f32) -> CornerRadius {
    CornerRadius {
        tl: value,
        tr: value,
        bl: 0.0,
        br: 0.0,
    }
}

/// Rounding on the bottom two corners only.
#[inline]
pub fn corner_radius_bottom(value: f32) -> CornerRadius {
    CornerRadius {
        tl: 0.0,
        tr: 0.0,
        bl: value,
        br: value,
    }
}

/// Fully transparent — the zero colour a container that paints nothing gets.
pub const TRANSPARENT: Color = Color {
    r: 0,
    g: 0,
    b: 0,
    a: 0,
};
