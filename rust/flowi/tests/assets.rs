//! Loads a TTF and a PNG by filesystem path through the default VFS mount, then renders with each.
//!
//! Both loads are asynchronous, so the test draws every frame until the rendered output shows up.

use flowi::{grow, layout, Application, Color, Font, Image, Painter, PixelView};

// Default clear color (theme surface.base).
const CLEAR: [u8; 3] = [14, 14, 16];

// The test font maps 'A' to a solid square glyph; the PNG is 4x4 solid red.
const FONT_PATH: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/tests/assets/test_font.ttf");
const IMAGE_PATH: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/tests/assets/test_image.png");

const MAX_TICKS: usize = 600;

const WHITE: Color = Color {
    r: 255,
    g: 255,
    b: 255,
    a: 255,
};

// The software backend reads back RGB24.
fn count_ink(pv: &PixelView) -> (usize, u16) {
    let w = pv.width as usize;
    let h = pv.height as usize;
    let mut ink = 0usize;
    let mut brightest = 0u16;
    for y in 0..h {
        for x in 0..w {
            let i = (y * w + x) * 3;
            let p = [pv.pixels[i], pv.pixels[i + 1], pv.pixels[i + 2]];
            if p != CLEAR {
                ink += 1;
                brightest = brightest.max(p[0] as u16 + p[1] as u16 + p[2] as u16);
            }
        }
    }
    (ink, brightest)
}

// The test paints the 4x4 PNG at (8, 8); ui scale is 1.0 headless.
fn has_red_near_origin(pv: &PixelView) -> bool {
    let w = pv.width as usize;
    for y in 4..20 {
        for x in 4..20 {
            let i = (y * w + x) * 3;
            let (r, g, b) = (pv.pixels[i], pv.pixels[i + 1], pv.pixels[i + 2]);
            if r > 150 && g < 100 && b < 100 {
                return true;
            }
        }
    }
    false
}

#[test]
fn font_and_image_load_by_path() {
    let mut app = Application::new(128, 96, true);
    let mut state = ();

    let font = Font::load(FONT_PATH, 32);
    assert_eq!(
        pv_channels(&mut app),
        3,
        "software backend reads back RGB24"
    );

    let mut saw_loaded = false;
    let mut font_frame = None;
    for _ in 0..MAX_TICKS {
        app.tick(&mut state, |_: &mut ()| {
            // The painter draws inside an open layout scope.
            layout! { width: grow!(), height: grow!() {
                Painter::text(8.0, 32.0, "A", font, WHITE);
            }}
        });
        saw_loaded |= Font::is_loaded(font);
        let frame = app.read_pixels().expect("render backend readback");
        if count_ink(&frame).0 > 20 {
            font_frame = Some(frame);
            break;
        }
    }
    assert!(
        saw_loaded,
        "Font::is_loaded should report the TTF ready within the frame budget"
    );
    let frame = font_frame.expect("the TTF glyph should ink the surface within the frame budget");
    let (ink, brightest) = count_ink(&frame);
    let surface = frame.width as usize * frame.height as usize;
    assert!(
        ink < surface / 2,
        "glyph should not fill the surface, but {ink} pixels are inked"
    );
    assert!(
        brightest > 600,
        "brightest inked pixel sum {brightest} should be near white (765 = pure white)"
    );

    let image = Image::load(IMAGE_PATH);
    let mut saw_ready = false;
    let mut found_red = false;
    for _ in 0..MAX_TICKS {
        app.tick(&mut state, |_: &mut ()| {
            layout! { width: grow!(), height: grow!() {
                Painter::image(8.0, 8.0, 4.0, 4.0, image, WHITE);
            }}
        });
        saw_ready |= Image::is_ready(image);
        if has_red_near_origin(&app.read_pixels().expect("render backend readback")) {
            found_red = true;
            break;
        }
    }
    assert!(
        saw_ready,
        "Image::is_ready should report the PNG decoded within the frame budget"
    );
    assert!(
        found_red,
        "the decoded red PNG should render near (8, 8) within the frame budget"
    );
}

fn pv_channels(app: &mut Application) -> usize {
    app.tick(&mut (), |_: &mut ()| {});
    app.read_pixels().expect("render backend readback").channels
}
