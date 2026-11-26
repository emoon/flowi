//! Constructors and conversions for the layout types, so a config can be spelled the short
//! way (radius: 4.0, padding: (8, 12), ..Default::default()) instead of naming every field.

use super::{
    BorderWidth, BorderWidthConfig, Color, CornerRadius, LayoutAlignmentX, LayoutAlignmentY,
    LayoutBorder, LayoutChildAlignment, LayoutClip, LayoutConfig, LayoutDirection, LayoutPadding,
    LayoutPlaced, LayoutSizing, LayoutSizingAxis, LayoutSizingAxisSize, LayoutSizingMinMax,
    LayoutSizingType, ScrollAreaConfig, ScrollbarDisplayMode, StrokeStyle, Vec2,
};

impl Color {
    /// Fully transparent (all channels zero).
    pub const TRANSPARENT: Color = Color::rgba(0, 0, 0, 0);

    /// Opaque RGB color (`a = 255`).
    pub const fn rgb(r: u8, g: u8, b: u8) -> Color {
        Color { r, g, b, a: 255 }
    }

    /// RGBA color from 8-bit channels.
    pub const fn rgba(r: u8, g: u8, b: u8, a: u8) -> Color {
        Color { r, g, b, a }
    }

    /// This color with its alpha channel replaced by `a`.
    pub const fn with_alpha(self, a: u8) -> Color {
        Color { a, ..self }
    }
}

impl Default for Color {
    fn default() -> Color {
        Color::TRANSPARENT
    }
}

impl PartialEq for Color {
    fn eq(&self, other: &Self) -> bool {
        (self.r, self.g, self.b, self.a) == (other.r, other.g, other.b, other.a)
    }
}

impl Eq for Color {}

impl Vec2 {
    /// The origin `(0.0, 0.0)`.
    pub const ZERO: Vec2 = Vec2 { x: 0.0, y: 0.0 };

    /// Construct a 2D vector from `(x, y)`.
    pub const fn new(x: f32, y: f32) -> Vec2 {
        Vec2 { x, y }
    }
}

impl Default for Vec2 {
    fn default() -> Vec2 {
        Vec2::ZERO
    }
}

impl StrokeStyle {
    /// Solid stroke (no dashes).
    pub const SOLID: StrokeStyle = StrokeStyle {
        dash_len: 0.0,
        gap_len: 0.0,
    };
}

impl Default for StrokeStyle {
    fn default() -> StrokeStyle {
        StrokeStyle::SOLID
    }
}

impl BorderWidthConfig {
    /// Zero width on every edge and between children.
    pub const NONE: BorderWidthConfig = BorderWidthConfig::all(0);

    /// Uniform width on all four outer edges, with no child dividers.
    pub const fn all(width: u16) -> BorderWidthConfig {
        BorderWidthConfig {
            left: width,
            right: width,
            top: width,
            bottom: width,
            between_children: 0,
        }
    }

    /// Top edge only.
    pub const fn top(width: u16) -> BorderWidthConfig {
        BorderWidthConfig {
            top: width,
            ..BorderWidthConfig::NONE
        }
    }

    /// Bottom edge only.
    pub const fn bottom(width: u16) -> BorderWidthConfig {
        BorderWidthConfig {
            bottom: width,
            ..BorderWidthConfig::NONE
        }
    }

    /// Left edge only.
    pub const fn left(width: u16) -> BorderWidthConfig {
        BorderWidthConfig {
            left: width,
            ..BorderWidthConfig::NONE
        }
    }

    /// Right edge only.
    pub const fn right(width: u16) -> BorderWidthConfig {
        BorderWidthConfig {
            right: width,
            ..BorderWidthConfig::NONE
        }
    }
}

impl Default for BorderWidthConfig {
    fn default() -> BorderWidthConfig {
        BorderWidthConfig::NONE
    }
}

impl PartialEq for BorderWidthConfig {
    fn eq(&self, other: &Self) -> bool {
        (
            self.left,
            self.right,
            self.top,
            self.bottom,
            self.between_children,
        ) == (
            other.left,
            other.right,
            other.top,
            other.bottom,
            other.between_children,
        )
    }
}

impl Eq for BorderWidthConfig {}

impl From<u16> for BorderWidthConfig {
    fn from(width: u16) -> BorderWidthConfig {
        BorderWidthConfig::all(width)
    }
}

impl From<BorderWidth> for BorderWidthConfig {
    fn from(width: BorderWidth) -> BorderWidthConfig {
        BorderWidthConfig::all(width as u16)
    }
}

impl LayoutBorder {
    /// No border (transparent, zero width).
    pub const NONE: LayoutBorder = LayoutBorder::solid(Color::TRANSPARENT, BorderWidthConfig::NONE);

    /// Solid border with uniform `width` on all four outer edges.
    pub const fn all(color: Color, width: u16) -> LayoutBorder {
        LayoutBorder::solid(color, BorderWidthConfig::all(width))
    }

    /// Solid border with caller-chosen per-edge widths.
    pub const fn solid(color: Color, width: BorderWidthConfig) -> LayoutBorder {
        LayoutBorder {
            color,
            width,
            stroke: StrokeStyle::SOLID,
        }
    }
}

impl Default for LayoutBorder {
    fn default() -> LayoutBorder {
        LayoutBorder::NONE
    }
}

impl LayoutSizing {
    /// Fit content along both axes.
    pub const FIT: LayoutSizing = LayoutSizing::new(LayoutSizingAxis::FIT, LayoutSizingAxis::FIT);

    /// Grow to fill available space along both axes.
    pub const GROW: LayoutSizing =
        LayoutSizing::new(LayoutSizingAxis::GROW, LayoutSizingAxis::GROW);

    /// Construct sizing from explicit width and height axes.
    pub const fn new(width: LayoutSizingAxis, height: LayoutSizingAxis) -> LayoutSizing {
        LayoutSizing { width, height }
    }
}

impl Default for LayoutSizing {
    fn default() -> LayoutSizing {
        LayoutSizing::FIT
    }
}

impl LayoutChildAlignment {
    /// Top-left origin.
    pub const TOP_LEFT: LayoutChildAlignment = LayoutChildAlignment {
        x: LayoutAlignmentX::Left,
        y: LayoutAlignmentY::Top,
    };

    /// Left-aligned horizontally, centered vertically.
    pub const CENTER_LEFT: LayoutChildAlignment = LayoutChildAlignment {
        x: LayoutAlignmentX::Left,
        y: LayoutAlignmentY::Center,
    };

    /// Centered along both axes.
    pub const CENTER: LayoutChildAlignment = LayoutChildAlignment {
        x: LayoutAlignmentX::Center,
        y: LayoutAlignmentY::Center,
    };

    /// Right-aligned horizontally, centered vertically.
    pub const CENTER_RIGHT: LayoutChildAlignment = LayoutChildAlignment {
        x: LayoutAlignmentX::Right,
        y: LayoutAlignmentY::Center,
    };
}

impl Default for LayoutChildAlignment {
    fn default() -> LayoutChildAlignment {
        LayoutChildAlignment::TOP_LEFT
    }
}

impl Default for ScrollAreaConfig {
    /// Full-size vertical scroll area with transparent background and auto scrollbar.
    fn default() -> ScrollAreaConfig {
        ScrollAreaConfig {
            sizing: LayoutSizing::GROW,
            direction: LayoutDirection::TopToBottom,
            child_gap: 0,
            padding: LayoutPadding::from(0),
            background_color: Color::TRANSPARENT,
            corner_radius: CornerRadius::uniform(0.0),
            show_scrollbar: true,
            scrollbar_mode: ScrollbarDisplayMode::Auto,
            smooth_scroll: false,
            scroll_speed: 0.0,
        }
    }
}

impl CornerRadius {
    /// The same radius on all four corners.
    pub const fn uniform(radius: f32) -> CornerRadius {
        CornerRadius {
            tl: radius,
            tr: radius,
            bl: radius,
            br: radius,
        }
    }

    /// Round the left and right corner pairs independently, as for a segmented control's end caps.
    pub const fn sides(left: f32, right: f32) -> CornerRadius {
        CornerRadius {
            tl: left,
            bl: left,
            tr: right,
            br: right,
        }
    }
}

impl From<f32> for CornerRadius {
    /// A scalar means a uniform radius.
    fn from(radius: f32) -> CornerRadius {
        CornerRadius::uniform(radius)
    }
}

/// The padding: layout field takes any of these, spelled in CSS's order.
///
/// u16 is the only integer source, so an unsuffixed literal (padding: 8) infers.
impl From<u16> for LayoutPadding {
    /// One value: the same inset on all four sides.
    fn from(pad: u16) -> LayoutPadding {
        LayoutPadding {
            left: pad,
            right: pad,
            top: pad,
            bottom: pad,
        }
    }
}

impl From<(u16, u16)> for LayoutPadding {
    /// CSS's two-value shorthand: (vertical, horizontal).
    fn from((vertical, horizontal): (u16, u16)) -> LayoutPadding {
        LayoutPadding {
            left: horizontal,
            right: horizontal,
            top: vertical,
            bottom: vertical,
        }
    }
}

impl From<[u16; 4]> for LayoutPadding {
    /// CSS's four-value shorthand, clockwise from the top: `[top, right, bottom, left]`. This
    /// is not the struct's field order.
    fn from([top, right, bottom, left]: [u16; 4]) -> LayoutPadding {
        LayoutPadding {
            left,
            right,
            top,
            bottom,
        }
    }
}

impl LayoutSizingAxis {
    /// Size to the content, with no bounds.
    pub const FIT: LayoutSizingAxis = LayoutSizingAxis {
        size: LayoutSizingAxisSize {
            min_max: LayoutSizingMinMax { min: 0.0, max: 0.0 },
        },
        r#type: LayoutSizingType::Fit,
    };

    /// Take the free space, with no bounds.
    pub const GROW: LayoutSizingAxis = LayoutSizingAxis {
        size: LayoutSizingAxisSize {
            min_max: LayoutSizingMinMax { min: 0.0, max: 0.0 },
        },
        r#type: LayoutSizingType::Grow,
    };
}

impl Default for LayoutConfig {
    /// The neutral container: Fit sizing with no bounds, no padding or gap, square corners,
    /// children at the left/top origin, LeftToRight, transparent, unclipped, no border, and
    /// normal flex flow rather than explicit placement. The layout macros expand an omitted
    /// field to the matching field of this value.
    fn default() -> LayoutConfig {
        LayoutConfig {
            sizing: LayoutSizing::FIT,
            padding: LayoutPadding::from(0),
            child_gap: 0,
            radius: CornerRadius::uniform(0.0),
            child_alignment: LayoutChildAlignment::TOP_LEFT,
            direction: LayoutDirection::LeftToRight,
            color: Color::TRANSPARENT,
            clip: LayoutClip {
                horizontal: false,
                vertical: false,
                manual_scroll: false,
                scroll_pos: Vec2::ZERO,
            },
            border: LayoutBorder::NONE,
            placed: LayoutPlaced {
                offset: Vec2::ZERO,
                enabled: false,
            },
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn padding_shorthands_map_to_the_right_sides() {
        let uniform = LayoutPadding::from(5);
        assert_eq!(
            (uniform.left, uniform.right, uniform.top, uniform.bottom),
            (5, 5, 5, 5)
        );

        let pair = LayoutPadding::from((3, 9));
        assert_eq!(
            (pair.top, pair.bottom, pair.left, pair.right),
            (3, 3, 9, 9),
            "(vertical, horizontal) must not land on the wrong axis"
        );

        let sides = LayoutPadding::from([1, 2, 3, 4]);
        assert_eq!(
            (sides.top, sides.right, sides.bottom, sides.left),
            (1, 2, 3, 4),
            "[u16; 4] is [top, right, bottom, left]"
        );
    }

    #[test]
    fn corner_radius_shorthands() {
        let u = CornerRadius::uniform(2.0);
        assert_eq!((u.tl, u.tr, u.bl, u.br), (2.0, 2.0, 2.0, 2.0));

        let s = CornerRadius::sides(1.0, 7.0);
        assert_eq!(
            (s.tl, s.bl, s.tr, s.br),
            (1.0, 1.0, 7.0, 7.0),
            "sides() rounds the left and right pairs independently"
        );

        let f = CornerRadius::from(3.0);
        assert_eq!(
            (f.tl, f.tr, f.bl, f.br),
            (3.0, 3.0, 3.0, 3.0),
            "From<f32> is uniform"
        );
    }

    #[test]
    fn default_layout_config_is_the_neutral_container() {
        let d = LayoutConfig::default();

        for (axis, name) in [(d.sizing.width, "width"), (d.sizing.height, "height")] {
            assert!(
                matches!(axis.r#type, LayoutSizingType::Fit),
                "{name} should default to Fit"
            );
            // SAFETY: the min_max arm is the live one for Fit, per LayoutSizingAxis::size.
            let min_max = unsafe { axis.size.min_max };
            assert_eq!((min_max.min, min_max.max), (0.0, 0.0), "{name} bounds");
        }

        assert_eq!(
            (
                d.padding.left,
                d.padding.right,
                d.padding.top,
                d.padding.bottom
            ),
            (0, 0, 0, 0)
        );
        assert_eq!(d.child_gap, 0);
        assert_eq!(
            (d.radius.tl, d.radius.tr, d.radius.bl, d.radius.br),
            (0.0, 0.0, 0.0, 0.0)
        );
        assert!(matches!(d.child_alignment.x, LayoutAlignmentX::Left));
        assert!(matches!(d.child_alignment.y, LayoutAlignmentY::Top));
        assert!(matches!(d.direction, LayoutDirection::LeftToRight));
        assert_eq!(
            (d.color.r, d.color.g, d.color.b, d.color.a),
            (0, 0, 0, 0),
            "a default container paints nothing"
        );

        assert!(!d.clip.horizontal);
        assert!(!d.clip.vertical);
        assert!(!d.clip.manual_scroll);
        assert_eq!((d.clip.scroll_pos.x, d.clip.scroll_pos.y), (0.0, 0.0));

        assert_eq!(
            (
                d.border.color.r,
                d.border.color.g,
                d.border.color.b,
                d.border.color.a
            ),
            (0, 0, 0, 0)
        );
        let bw = d.border.width;
        assert_eq!(
            (bw.left, bw.right, bw.top, bw.bottom, bw.between_children),
            (0, 0, 0, 0, 0)
        );
        assert_eq!(
            (d.border.stroke.dash_len, d.border.stroke.gap_len),
            (0.0, 0.0)
        );

        assert!(
            !d.placed.enabled,
            "a default child flows, it is not explicitly placed"
        );
        assert_eq!((d.placed.offset.x, d.placed.offset.y), (0.0, 0.0));
    }

    #[test]
    fn color_and_vec2_constructors() {
        assert_eq!(Color::default(), Color::TRANSPARENT);
        assert_eq!(Color::rgb(10, 20, 30), Color::rgba(10, 20, 30, 255));
        assert_eq!(
            Color::rgb(10, 20, 30).with_alpha(128),
            Color::rgba(10, 20, 30, 128)
        );
        let z = Vec2::default();
        assert_eq!((z.x, z.y), (0.0, 0.0));
        let p = Vec2::new(3.0, 4.0);
        assert_eq!((p.x, p.y), (3.0, 4.0));
    }

    #[test]
    fn border_and_scroll_constructors() {
        let all = BorderWidthConfig::all(2);
        assert_eq!(all, BorderWidthConfig::from(2u16));
        assert_eq!(all, BorderWidthConfig::from(BorderWidth::Medium));
        assert_eq!(
            (
                all.left,
                all.right,
                all.top,
                all.bottom,
                all.between_children
            ),
            (2, 2, 2, 2, 0)
        );
        assert_eq!(BorderWidthConfig::top(1).top, 1);
        assert_eq!(BorderWidthConfig::top(1).bottom, 0);
        assert_eq!(BorderWidthConfig::bottom(3).bottom, 3);
        assert_eq!(BorderWidthConfig::left(4).left, 4);
        assert_eq!(BorderWidthConfig::right(5).right, 5);

        let b = LayoutBorder::all(Color::rgb(1, 2, 3), 2);
        assert_eq!(b.color, Color::rgb(1, 2, 3));
        assert_eq!(b.width, BorderWidthConfig::all(2));
        assert_eq!((b.stroke.dash_len, b.stroke.gap_len), (0.0, 0.0));

        let s = ScrollAreaConfig::default();
        assert_eq!(s.background_color, Color::TRANSPARENT);
        assert!(s.show_scrollbar);
        assert!(matches!(s.scrollbar_mode, ScrollbarDisplayMode::Auto));
        assert!(matches!(s.sizing.width.r#type, LayoutSizingType::Grow));
        assert!(matches!(s.sizing.height.r#type, LayoutSizingType::Grow));
    }
}
