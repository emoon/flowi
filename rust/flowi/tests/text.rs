//! The built-in 8x8 default font is active, so the text renders with no shipped asset.

use flowi::{grow, layout, Application, Color, FontId, Painter};

// Default clear color (theme surface.base).
const CLEAR: [u8; 3] = [14, 14, 16];

#[test]
fn painter_text_hello_world() {
    // ui_scale is 1.0, so the (8, 32) reference position maps 1:1 to pixels. The
    // 16px default font (scale 2) leaves the band comfortably inside the surface.
    let mut app = Application::new(256, 64, true);

    let mut state = ();
    assert!(
        app.tick(&mut state, |_: &mut ()| {
            // The painter draws inside an open layout scope.
            layout! { width: grow!(), height: grow!() {
                let white = Color { r: 255, g: 255, b: 255, a: 255 };
                Painter::text(8.0, 32.0, format!("Hello, {}!", "world"), FontId(0), white);
            }}
        }),
        "first tick should keep running"
    );

    let pv = app.read_pixels().expect("render backend readback");
    assert_eq!(pv.channels, 3, "software backend reads back RGB24");
    assert_eq!(pv.pixels.len(), 256 * 64 * 3, "tightly-packed RGB24");

    let w = pv.width as usize;
    let h = pv.height as usize;
    let px = |x: usize, y: usize| {
        let i = (y * w + x) * 3;
        [pv.pixels[i], pv.pixels[i + 1], pv.pixels[i + 2]]
    };

    let mut ink = 0usize;
    let mut brightest = 0u16;
    for y in 0..h {
        for x in 0..w {
            let p = px(x, y);
            if p != CLEAR {
                ink += 1;
                brightest = brightest.max(p[0] as u16 + p[1] as u16 + p[2] as u16);
            }
        }
    }

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
    for y in 48..h {
        for x in 0..w {
            assert_eq!(
                px(x, y),
                CLEAR,
                "row {y} px {x} should be clear color below the text band"
            );
        }
    }
}
