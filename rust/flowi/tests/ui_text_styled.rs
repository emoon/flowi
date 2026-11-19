//! The run is styled by a TextConfig with a non-default colour (pure red) and vertical alignment (Top).
//! Pure red {255, 0, 0} in the readback, not the theme primary {240, 240, 240}, shows config.color
//! arrived; the valign offset is not pixel-asserted.

mod common;

use common::Readback;
use flowi::{align, col, grow, text_styled, Application, Color, TextConfig, TextVAlign};

fn frame(_: &mut ()) {
    // The painter path draws at a fixed top-left (8, 32) regardless of layout; a layout-participating
    // run honours the alignment.
    col! { width: grow!(), height: grow!(), align: align!(Right, Bottom) {
        // Owned temporary.
        text_styled(
            format!("{}!", "Styled"),
            TextConfig {
                color: Color { r: 255, g: 0, b: 0, a: 255 },
                valign: TextVAlign::Top,
                ..Default::default()
            },
        );
    }}
}

fn assert_config_red(brightest: [u8; 3]) {
    assert!(
        brightest[0] >= 232,
        "brightest pixel red channel = {} should be the config red 255, got {brightest:?}",
        brightest[0]
    );
    assert!(
        brightest[1] < 60 && brightest[2] < 60,
        "brightest pixel {brightest:?} should be red-dominant — green/blue near zero, not the grey theme primary"
    );
}

#[test]
fn ui_text_styled_layout_config_color() {
    // ui_scale is 1.0, so window px map 1:1 to layout px. The 16px default font (scale 2) gives a
    // "Styled!" run wide/tall enough to ink a clear bottom-right band.
    let mut app = Application::new(256, 96, true);

    let mut state = ();
    assert!(app.tick(&mut state, frame), "first tick should keep running");

    let shot = Readback::capture(&mut app);
    assert_eq!(shot.byte_len(), 256 * 96 * 3, "tightly-packed RGB24");

    let (w, h) = (shot.width(), shot.height());

    // A 1-bit font at integer scale inks glyph interiors at full coverage, so the brightest inked pixel is the
    // unblended source colour: the config colour, the value under test.
    let (ink, brightest) = shot.ink_and_brightest();

    // "Styled!" is 7 glyphs of a 16px font: many inked pixels, far from the whole surface.
    assert!(ink > 50, "expected the text to ink many pixels, got {ink}");
    assert!(
        ink < w * h / 2,
        "text should not fill the surface, but {ink} pixels are inked"
    );

    assert_config_red(brightest);

    assert_eq!(
        shot.inked_in((0, 0), (128, 56)),
        0,
        "top-left region (painter's fixed origin) should be bare — the run aligned bottom-right, not painter-drawn"
    );
    assert!(
        shot.inked_in((128, 56), (w, h)) > 50,
        "bottom-right region should hold the layout-placed run"
    );
}
