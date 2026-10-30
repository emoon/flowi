//! Constructors and conversions for the layout types, so a config can be spelled the short
//! way - radius: 4.0, padding: (8, 12), ..Default::default() - instead of naming every field.

use super::{
    BorderWidthConfig, Color, CornerRadius, LayoutAlignmentX, LayoutAlignmentY, LayoutBorder,
    LayoutChildAlignment, LayoutClip, LayoutConfig, LayoutDirection, LayoutPadding, LayoutPlaced,
    LayoutSizing, LayoutSizingAxis, LayoutSizingAxisSize, LayoutSizingMinMax, LayoutSizingType,
    StrokeStyle, Vec2,
};

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

    /// Round the left and right corner pairs independently - the segmented-control end-cap case.
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
    /// CSS's four-value shorthand, clockwise from the top: `[top, right, bottom, left]` - not
    /// the struct's own field order.
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
        const TRANSPARENT: Color = Color {
            r: 0,
            g: 0,
            b: 0,
            a: 0,
        };

        LayoutConfig {
            sizing: LayoutSizing {
                width: LayoutSizingAxis::FIT,
                height: LayoutSizingAxis::FIT,
            },
            padding: LayoutPadding::from(0),
            child_gap: 0,
            radius: CornerRadius::uniform(0.0),
            child_alignment: LayoutChildAlignment {
                x: LayoutAlignmentX::Left,
                y: LayoutAlignmentY::Top,
            },
            direction: LayoutDirection::LeftToRight,
            color: TRANSPARENT,
            clip: LayoutClip {
                horizontal: false,
                vertical: false,
                manual_scroll: false,
                scroll_pos: Vec2 { x: 0.0, y: 0.0 },
            },
            border: LayoutBorder {
                color: TRANSPARENT,
                width: BorderWidthConfig {
                    left: 0,
                    right: 0,
                    top: 0,
                    bottom: 0,
                    between_children: 0,
                },
                stroke: StrokeStyle {
                    dash_len: 0.0,
                    gap_len: 0.0,
                },
            },
            placed: LayoutPlaced {
                offset: Vec2 { x: 0.0, y: 0.0 },
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
}
