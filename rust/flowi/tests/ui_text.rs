//! Text::emit lays out a layout-participating element in the theme primary colour {240, 240, 240}.

mod common;

use common::Readback;
use flowi::{align, col, grow, Application, Text};

fn frame(_: &mut ()) {
    // A layout-participating run honours the alignment.
    col! { width: grow!(), height: grow!(), align: align!(Right, Bottom) {
        // Owned temporary: Text::emit copies before the drop.
        Text::emit(format!("{}!", "Layout"));
    }}
}

fn assert_theme_primary(brightest: [u8; 3]) {
    for (c, v) in brightest.iter().enumerate() {
        assert!(
            (232..=246).contains(v),
            "brightest pixel channel {c} = {v} should be the theme primary 240, got {brightest:?}"
        );
    }
    assert!(
        brightest.iter().all(|&v| v < 250),
        "brightest pixel {brightest:?} must not reach pure white (255)"
    );
}

#[test]
fn ui_text_layout_theme_primary() {
    // ui_scale is 1.0, so window px map 1:1 to layout px. The 16px default font (scale 2) gives a
    // "Layout!" run wide/tall enough to ink a clear bottom-right band.
    let mut app = Application::new(256, 96, true);

    let mut state = ();
    assert!(app.tick(&mut state, frame), "first tick should keep running");

    let shot = Readback::capture(&mut app);
    assert_eq!(shot.byte_len(), 256 * 96 * 3, "tightly-packed RGB24");

    let (w, h) = (shot.width(), shot.height());

    // A 1-bit font at integer scale inks glyph interiors at full coverage, so the brightest inked pixel is the
    // unblended source colour: the theme primary, the value under test.
    let (ink, brightest) = shot.ink_and_brightest();

    // "Layout!" is 7 glyphs of a 16px font: many inked pixels, far from the whole surface.
    assert!(ink > 50, "expected the text to ink many pixels, got {ink}");
    assert!(
        ink < w * h / 2,
        "text should not fill the surface, but {ink} pixels are inked"
    );

    assert_theme_primary(brightest);

    assert_eq!(
        shot.inked_in((0, 0), (128, 56)),
        0,
        "top-left region should be bare — the run aligned bottom-right"
    );
    assert!(
        shot.inked_in((128, 56), (w, h)) > 50,
        "bottom-right region should hold the layout-placed run"
    );
}
