//! The painter draws inside an element's scope, so the calls are wrapped in a full-window layout!
//! element at the origin. The element fills no background, so the paint maps 1:1 to pixels.
//!
//! The software backend renders rect, rect_styled, and the hline/vline hairlines, which snap so a 1px
//! line at integer coords lands on exactly one pixel row/column. The remaining calls are exercised
//! link-and-run only; the software backend does not draw the GPU-tier ones.

mod common;

use common::{Readback, CLEAR};
use flowi::{
    grow, layout, Application, Color, CornerColors, CornerRadius, FontId, Painter, RectConfig, Stroke,
    StrokeCap, TextStyle,
};

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

/// Pixel-verified primitives, in non-overlapping regions.
fn draw_verified_primitives() {
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
}

/// Link-and-run only, kept clear of the asserted pixels in the bottom band (y >= 44).
fn draw_link_only_calls() {
    let c = Color { r: 60, g: 120, b: 220, a: 255 };
    let none = Color { r: 0, g: 0, b: 0, a: 0 };
    let stroke = Stroke::new(2.0, c);
    Painter::circle(40.0, 50.0, 6.0, c);
    Painter::circle_stroke(40.0, 50.0, 6.0, stroke.with_dash(2.0, 1.0));
    Painter::circle_bordered(40.0, 50.0, 6.0, c, stroke);
    Painter::rect_stroke(4.0, 44.0, 16.0, 16.0, stroke);
    Painter::rect_rounded_stroke(4.0, 44.0, 16.0, 16.0, 4.0, stroke);
    Painter::line(4.0, 44.0, 20.0, 60.0, stroke);
    Painter::rect_rounded(4.0, 44.0, 16.0, 16.0, 4.0, c);
    Painter::gradient_rect(24.0, 44.0, 16.0, 16.0, CornerColors { top_left: c, top_right: none, bottom_left: none, bottom_right: c });
    Painter::diamond(44.0, 50.0, 6.0, 8.0, c);
    Painter::diamond_stroke(44.0, 50.0, 6.0, 8.0, stroke);
    Painter::diamond_bordered(44.0, 50.0, 6.0, 8.0, c, stroke);
    Painter::arc(44.0, 44.0, 6.0, 0.0, std::f32::consts::PI, stroke.with_cap(StrokeCap::Round));
    // Font 0 is the active font. The built-in font paints its cells above the pen baseline, so
    // anchor both runs deep in the unasserted bottom band.
    Painter::text(4.0, 60.0, "au", FontId(0), c);
    Painter::text_styled(40.0, 60.0, "au", TextStyle { font_id: 0, font_size: 7.5, letter_spacing: 0.5, tab_width: 0.0 }, c);
    // Invalid image handle (0): a no-op that still crosses the FFI boundary and returns false.
    assert!(!Painter::image(28.0, 4.0, 12.0, 12.0, flowi::ImageHandle(0), c), "invalid image handle draws nothing");
}

fn frame(_: &mut ()) {
    layout! { width: grow!(), height: grow!() {
        draw_verified_primitives();
        draw_link_only_calls();
    }}
}

/// The software color pipeline rounds ~1 LSB per channel, so compare within a small tolerance.
fn near(got: [u8; 3], want: [u8; 3]) -> bool {
    got.iter()
        .zip(want.iter())
        .all(|(g, w)| (i16::from(*g) - i16::from(*w)).abs() <= 2)
}

fn assert_near(shot: &Readback, (x, y): (usize, usize), color: Color, what: &str) {
    let want = [color.r, color.g, color.b];
    let got = shot.px(x, y);
    assert!(near(got, want), "{what} {got:?} should be ~{want:?}");
}

fn assert_clear(shot: &Readback, (x, y): (usize, usize), what: &str) {
    assert_eq!(shot.px(x, y), CLEAR, "{what}");
}

#[test]
fn painter_bindings_round_trip() {
    // ui_scale is 1.0, so painter coordinates map 1:1 to pixels.
    let mut app = Application::new(64, 64, true);

    let mut state = ();
    assert!(app.tick(&mut state, frame), "first tick should keep running");

    let shot = Readback::capture(&mut app);

    assert_near(&shot, (10, 10), RECT, "rect interior");
    assert_clear(&shot, (2, 2), "outside the rect stays the clear color");

    assert_near(&shot, (30, 24), STYLED, "rect_styled interior");
    assert_clear(&shot, (22, 24), "left of the styled rect stays the clear color");

    assert_near(&shot, (20, 30), LINE, "hline pixel");
    assert_clear(&shot, (20, 28), "two rows above the hline stays the clear color");
    assert_clear(&shot, (20, 32), "two rows below the hline stays the clear color");

    assert_near(&shot, (50, 20), LINE, "vline pixel");
    assert_clear(&shot, (48, 20), "two columns left of the vline stays the clear color");
    assert_clear(&shot, (52, 20), "two columns right of the vline stays the clear color");
}
