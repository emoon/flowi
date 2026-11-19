//! The built-in 8x8 default font is active, so the text renders with no shipped asset.

mod common;

use common::{Readback, CLEAR};
use flowi::{grow, layout, Application, Color, FontId, Painter};

fn frame(_: &mut ()) {
    // The painter draws inside an open layout scope.
    layout! { width: grow!(), height: grow!() {
        let white = Color { r: 255, g: 255, b: 255, a: 255 };
        Painter::text(8.0, 32.0, format!("Hello, {}!", "world"), FontId(0), white);
    }}
}

fn assert_clear_from_row(shot: &Readback, first_row: usize) {
    for y in first_row..shot.height() {
        for x in 0..shot.width() {
            assert_eq!(
                shot.px(x, y),
                CLEAR,
                "row {y} px {x} should be clear color below the text band"
            );
        }
    }
}

#[test]
fn painter_text_hello_world() {
    // ui_scale is 1.0, so the (8, 32) reference position maps 1:1 to pixels. The
    // 16px default font (scale 2) leaves the band comfortably inside the surface.
    let mut app = Application::new(256, 64, true);

    let mut state = ();
    assert!(app.tick(&mut state, frame), "first tick should keep running");

    let shot = Readback::capture(&mut app);
    assert_eq!(shot.byte_len(), 256 * 64 * 3, "tightly-packed RGB24");

    let (w, h) = (shot.width(), shot.height());
    let (ink, brightest_px) = shot.ink_and_brightest();
    let brightest: u16 = brightest_px.iter().map(|&c| c as u16).sum();

    // 13 glyphs of a 16px font ink hundreds of pixels, nowhere near the whole 16384-pixel surface.
    assert!(ink > 50, "expected the text to ink many pixels, got {ink}");
    assert!(
        ink < w * h / 2,
        "text should not fill the surface, but {ink} pixels are inked"
    );
    assert!(
        brightest > 600,
        "brightest inked pixel sum {brightest} should be near white (765 = pure white)"
    );

    // The text sits in the upper band around the (8, 32) baseline.
    assert_clear_from_row(&shot, 48);
}
