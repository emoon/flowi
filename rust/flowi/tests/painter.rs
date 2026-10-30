//! The painter draws inside an element's scope, so the calls are wrapped in a full-window layout!
//! element at the origin. The element fills no background, so the paint maps 1:1 to pixels.
//!
//! The software backend renders rect, rect_styled, and the hline/vline hairlines, which snap so a 1px
//! line at integer coords lands on exactly one pixel row/column. The remaining calls are exercised
//! link-and-run only; the software backend does not draw the GPU-tier ones.

use flowi::{
    grow, layout, Application, ArcFlags, Color, CornerRadius, FontId, Painter, RectConfig,
    TextStyle,
};

// Default clear color (theme surface.base).
const CLEAR: [u8; 3] = [14, 14, 16];
const RECT: Color = Color {
    r: 200,
    g: 60,
    b: 60,
    a: 255,
};
const LINE: Color = Color {
    r: 220,
    g: 220,
    b: 60,
    a: 255,
};
const STYLED: Color = Color {
    r: 60,
    g: 200,
    b: 120,
    a: 255,
};

#[test]
fn painter_bindings_round_trip() {
    // ui_scale is 1.0, so painter coordinates map 1:1 to pixels.
    let mut app = Application::new(64, 64, true);

    let mut state = ();
    assert!(
        app.tick(&mut state, |_: &mut ()| {
            let c = Color { r: 60, g: 120, b: 220, a: 255 };
            let none = Color { r: 0, g: 0, b: 0, a: 0 };
            layout! { width: grow!(), height: grow!() {
                // Pixel-verified primitives, in non-overlapping regions.
                // A 16x16 block at (4,4): interior pixel (10,10).
                Painter::rect(4.0, 4.0, 16.0, 16.0, RECT);
                // Horizontal hairline: row y=30, x in [4, 34).
                Painter::hline(4.0, 30.0, 30.0, LINE);
                // Vertical hairline: column x=50, y in [4, 34).
                Painter::vline(50.0, 4.0, 30.0, LINE);
                // A 12x8 block at (24,20), square corners and one color in all four: interior pixel (30,24).
                Painter::rect_styled(24.0, 20.0, 12.0, 8.0, RectConfig {
                    radius: CornerRadius { tl: 0.0, tr: 0.0, bl: 0.0, br: 0.0 },
                    corner: [STYLED; 4],
                });

                // Link-and-run only, kept clear of the asserted pixels above in the bottom band (y >= 44).
                Painter::circle(40.0, 50.0, 6.0, c, none, 0.0);
                Painter::rect_rounded(4.0, 44.0, 16.0, 16.0, 4.0, c);
                Painter::gradient_rect(24.0, 44.0, 16.0, 16.0, c, none, none, c);
                Painter::diamond(44.0, 50.0, 6.0, 8.0, c, none, 0.0);
                Painter::arc(44.0, 44.0, 6.0, 2.0, 0.0, std::f32::consts::PI, c, ArcFlags::ROUND_CAPS);
                // Font 0 is the active font. The built-in font paints its cells above the pen baseline, so
                // anchor both runs deep in the unasserted bottom band.
                Painter::text(4.0, 60.0, "au", FontId(0), c);
                Painter::text_styled(40.0, 60.0, "au", TextStyle { font_id: 0, font_size: 7.5, letter_spacing: 0.5 }, c);
                // Invalid image handle (0): a no-op that still crosses the FFI boundary and returns false.
                assert!(!Painter::image(28.0, 4.0, 12.0, 12.0, flowi::ImageHandle(0), c), "invalid image handle draws nothing");
            }}
        }),
        "first tick should keep running"
    );

    let pv = app.read_pixels().expect("render backend readback");
    assert_eq!(pv.channels, 3, "software backend reads back RGB24");

    let px = |x: usize, y: usize| {
        let i = (y * pv.width as usize + x) * 3;
        [pv.pixels[i], pv.pixels[i + 1], pv.pixels[i + 2]]
    };
    // The software color pipeline rounds ~1 LSB per channel, so compare within a small tolerance.
    let near = |got: [u8; 3], want: [u8; 3]| {
        got.iter()
            .zip(want.iter())
            .all(|(g, w)| (i16::from(*g) - i16::from(*w)).abs() <= 2)
    };

    let rect_want = [RECT.r, RECT.g, RECT.b];
    assert!(
        near(px(10, 10), rect_want),
        "rect interior {:?} should be ~{:?}",
        px(10, 10),
        rect_want
    );
    assert_eq!(px(2, 2), CLEAR, "outside the rect stays the clear color");

    let styled_want = [STYLED.r, STYLED.g, STYLED.b];
    assert!(
        near(px(30, 24), styled_want),
        "rect_styled interior {:?} should be ~{:?}",
        px(30, 24),
        styled_want
    );
    assert_eq!(
        px(22, 24),
        CLEAR,
        "left of the styled rect stays the clear color"
    );

    let line_want = [LINE.r, LINE.g, LINE.b];
    assert!(
        near(px(20, 30), line_want),
        "hline pixel {:?} should be ~{:?}",
        px(20, 30),
        line_want
    );
    assert_eq!(
        px(20, 28),
        CLEAR,
        "two rows above the hline stays the clear color"
    );
    assert_eq!(
        px(20, 32),
        CLEAR,
        "two rows below the hline stays the clear color"
    );

    assert!(
        near(px(50, 20), line_want),
        "vline pixel {:?} should be ~{:?}",
        px(50, 20),
        line_want
    );
    assert_eq!(
        px(48, 20),
        CLEAR,
        "two columns left of the vline stays the clear color"
    );
    assert_eq!(
        px(52, 20),
        CLEAR,
        "two columns right of the vline stays the clear color"
    );
}
