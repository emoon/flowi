//! Text and font measurement bindings against the built-in default font.

use flowi::{Application, Font, FontId, Text, TextStyle};

/// Metrics within this many px of the expected cell math count as a match.
const TOLERANCE: f32 = 0.5;

/// The built-in 8x8 default font (scale 2 → 16px cells) gives cell math: n cells wide plus n−1
/// letter-spacing gaps, cell-height line metrics, and font_id/size zeroes falling back to the active
/// font at its default size.
#[test]
fn text_measure_reports_default_font_cells() {
    let mut app = Application::new(64, 32, true);

    let mut state = ();
    assert!(
        app.tick(&mut state, |_: &mut ()| {
            let m = Text::measure(
                String::from("AB"), // owned temporary
                TextStyle {
                    font_id: 0,
                    font_size: 0.0,
                    letter_spacing: 2.0,
                },
            );
            assert!(
                (m.width - 34.0).abs() < TOLERANCE,
                "two 16px cells plus one spacing gap (n-1), got {}",
                m.width
            );
            assert!(
                (m.height - 16.0).abs() < TOLERANCE,
                "line height is the cell height, got {}",
                m.height
            );

            let fm = Font::metrics(FontId(0), 0.0);
            assert!(
                (fm.line_height - 16.0).abs() < TOLERANCE,
                "font id 0 resolves to the active font at its default size, got {}",
                fm.line_height
            );
        }),
        "first tick should keep running"
    );
}
