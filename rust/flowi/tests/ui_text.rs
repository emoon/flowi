//! Text::emit lays out a layout-participating element in the theme primary colour {240, 240, 240}.

use flowi::{align, col, grow, Application, Text};

// Default clear color (theme surface.base).
const CLEAR: [u8; 3] = [14, 14, 16];

#[test]
fn ui_text_layout_theme_primary() {
    // ui_scale is 1.0, so window px map 1:1 to layout px. The 16px default font (scale 2) gives a
    // "Layout!" run wide/tall enough to ink a clear bottom-right band.
    let mut app = Application::new(256, 96, true);

    let mut state = ();
    assert!(
        app.tick(&mut state, |_: &mut ()| {
            // A layout-participating run honours the alignment.
            col! { width: grow!(), height: grow!(), align: align!(Right, Bottom) {
                // Owned temporary: Text::emit copies before the drop.
                Text::emit(format!("{}!", "Layout"));
            }}
        }),
        "first tick should keep running"
    );

    let pv = app.read_pixels().expect("render backend readback");
    assert_eq!(pv.channels, 3, "software backend reads back RGB24");
    assert_eq!(pv.pixels.len(), 256 * 96 * 3, "tightly-packed RGB24");

    let w = pv.width as usize;
    let h = pv.height as usize;
    let px = |x: usize, y: usize| {
        let i = (y * w + x) * 3;
        [pv.pixels[i], pv.pixels[i + 1], pv.pixels[i + 2]]
    };

    // A 1-bit font at integer scale inks glyph interiors at full coverage, so the brightest inked pixel is the
    // unblended source colour - the theme primary, the value under test.
    let mut ink = 0usize;
    let mut brightest = [0u8; 3];
    for y in 0..h {
        for x in 0..w {
            let p = px(x, y);
            if p != CLEAR {
                ink += 1;
                if p[0] as u16 + p[1] as u16 + p[2] as u16
                    > brightest[0] as u16 + brightest[1] as u16 + brightest[2] as u16
                {
                    brightest = p;
                }
            }
        }
    }

    // "Layout!" is 7 glyphs of a 16px font - many inked pixels, far from the whole surface.
    assert!(ink > 50, "expected the text to ink many pixels, got {ink}");
    assert!(
        ink < w * h / 2,
        "text should not fill the surface, but {ink} pixels are inked"
    );

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

    let inked_in = |x0: usize, y0: usize, x1: usize, y1: usize| {
        let mut n = 0usize;
        for y in y0..y1 {
            for x in x0..x1 {
                if px(x, y) != CLEAR {
                    n += 1;
                }
            }
        }
        n
    };
    assert_eq!(
        inked_in(0, 0, 128, 56),
        0,
        "top-left region should be bare — the run aligned bottom-right"
    );
    assert!(
        inked_in(128, 56, w, h) > 50,
        "bottom-right region should hold the layout-placed run"
    );
}
