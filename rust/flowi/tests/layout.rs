//! The outer fixed container is the single top-level element, so it is placed at the top-left (0, 0); it holds
//! two fixed children laid left-to-right.

use flowi::{
    Application, BorderWidthConfig, Color, CornerRadius, LayoutAlignmentX, LayoutAlignmentY,
    LayoutBorder, LayoutChildAlignment, LayoutClip, LayoutConfig, LayoutDirection, LayoutPadding,
    LayoutPlaced, LayoutSizing, LayoutSizingAxis, LayoutSizingAxisSize, LayoutSizingMinMax,
    LayoutSizingType, StrokeStyle, Vec2,
};

// Default clear color (theme surface.base).
const CLEAR: [u8; 3] = [14, 14, 16];
// Both far from CLEAR and from each other so every region is unambiguous.
const OUTER: [u8; 3] = [220, 40, 40];
const CHILD: [u8; 3] = [40, 200, 90];

// Geometry (reference px == device px at ui_scale 1.0).
const PAD: u16 = 8;
const GAP: u16 = 6;
const CW: f32 = 12.0;
const CH: f32 = 24.0;
const OW: f32 = 2.0 * CW + GAP as f32 + 2.0 * PAD as f32; // 8+12+6+12+8 = 46
const OH: f32 = CH + 2.0 * PAD as f32; // 8+24+8 = 40

// min == max, type Fixed - the union's min_max arm.
fn fixed(px: f32) -> LayoutSizingAxis {
    LayoutSizingAxis {
        size: LayoutSizingAxisSize {
            min_max: LayoutSizingMinMax { min: px, max: px },
        },
        r#type: LayoutSizingType::Fixed,
    }
}

// Borders/radius/clip are zeroed - not exercised here.
fn config(w: f32, h: f32, color: [u8; 3], pad: u16, gap: u16) -> LayoutConfig {
    LayoutConfig {
        sizing: LayoutSizing {
            width: fixed(w),
            height: fixed(h),
        },
        padding: LayoutPadding {
            left: pad,
            right: pad,
            top: pad,
            bottom: pad,
        },
        child_gap: gap,
        radius: CornerRadius {
            tl: 0.0,
            tr: 0.0,
            bl: 0.0,
            br: 0.0,
        },
        child_alignment: LayoutChildAlignment {
            x: LayoutAlignmentX::Left,
            y: LayoutAlignmentY::Top,
        },
        direction: LayoutDirection::LeftToRight,
        color: Color {
            r: color[0],
            g: color[1],
            b: color[2],
            a: 255,
        },
        clip: LayoutClip {
            horizontal: false,
            vertical: false,
            manual_scroll: false,
            scroll_pos: Vec2 { x: 0.0, y: 0.0 },
        },
        border: LayoutBorder {
            color: Color {
                r: 0,
                g: 0,
                b: 0,
                a: 0,
            },
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

#[test]
fn layout_padding_gap_color_render() {
    // ui_scale is 1.0 at this size, so reference px equal window px.
    let mut app = Application::new(64, 64, true);

    let outer = config(OW, OH, OUTER, PAD, GAP);
    let child = config(CW, CH, CHILD, 0, 0);

    let mut state = ();
    assert!(
        app.tick(&mut state, move |_: &mut ()| {
            flowi::Layout::start("root");
            flowi::Layout::configure(outer);
            // format! temporary id: the id is copied into the per-frame arena before the temporary drops.
            flowi::Layout::start(format!("child-{}", 0));
            flowi::Layout::configure(child);
            flowi::Layout::end();
            flowi::Layout::start("child-1");
            flowi::Layout::configure(child);
            flowi::Layout::end();
            flowi::Layout::end();
        }),
        "first tick should keep running"
    );

    let pv = app.read_pixels().expect("render backend readback");
    assert_eq!(pv.channels, 3, "software backend reads back RGB24");
    assert_eq!(pv.pixels.len(), 64 * 64 * 3, "tightly-packed RGB24");

    let w = pv.width as usize;
    let px = |x: usize, y: usize| {
        let i = (y * w + x) * 3;
        [pv.pixels[i], pv.pixels[i + 1], pv.pixels[i + 2]]
    };
    let near = |p: [u8; 3], q: [u8; 3]| {
        let d = |a: u8, b: u8| (a as i32 - b as i32).abs();
        d(p[0], q[0]) <= 8 && d(p[1], q[1]) <= 8 && d(p[2], q[2]) <= 8
    };

    // Child A occupies reference rect [PAD, PAD+CW) x [PAD, PAD+CH); child B starts CW+GAP further right.
    let ax = PAD as usize; // 8
    let bx = (PAD as f32 + CW + GAP as f32) as usize; // 8+12+6 = 26
    let cy = (PAD as f32 + CH / 2.0) as usize; // vertical middle of the children, 20

    assert!(
        near(px(ax + 6, cy), CHILD),
        "child A interior should be the child fill, got {:?}",
        px(ax + 6, cy)
    );
    assert!(
        near(px(bx + 6, cy), CHILD),
        "child B interior should be the child fill, got {:?}",
        px(bx + 6, cy)
    );

    assert!(
        near(px(0, 0), OUTER),
        "top-left corner should be the outer (padding) fill, got {:?}",
        px(0, 0)
    );
    assert!(
        near(px(2, cy), OUTER),
        "left padding band should be the outer fill, got {:?}",
        px(2, cy)
    );

    // The gap band is x in [PAD+CW, PAD+CW+GAP).
    let gap_mid = (PAD as f32 + CW + GAP as f32 / 2.0) as usize; // 8+12+3 = 23
    assert!(
        near(px(gap_mid, cy), OUTER),
        "gap between children should show the outer fill, got {:?}",
        px(gap_mid, cy)
    );

    assert!(
        near(px(OW as usize + 1, 0), CLEAR),
        "right of the container should be clear, got {:?}",
        px(OW as usize + 1, 0)
    );
    assert!(
        near(px(0, OH as usize + 1), CLEAR),
        "below the container should be clear, got {:?}",
        px(0, OH as usize + 1)
    );
}
