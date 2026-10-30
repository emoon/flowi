//! Every inset is a different size, so the array form's CSS order is checked side by side. The uniform row
//! uses an unsuffixed literal (`padding: 4`), which must infer to u16.

use flowi::{col, fit, fixed, grow, layout, placed, row, Application, Color, LayoutDirection};

const fn rgb(r: u8, g: u8, b: u8) -> Color {
    Color { r, g, b, a: 255 }
}

// Default clear color (theme surface.base).
const CLEAR: Color = rgb(14, 14, 16);
// The padding bands read as OUTER; each band's inner child gets its own colour so a band can never be confused
// with a neighbouring band's child.
const OUTER: Color = rgb(220, 40, 40);
const CHILD_A: Color = rgb(30, 180, 60);
const CHILD_B: Color = rgb(40, 80, 220);
const CHILD_C: Color = rgb(230, 200, 40);
const CHILD_D: Color = rgb(200, 60, 200);

// The root is a fixed 40x40 column at (0, 0) - four 10px-tall bands, no gap.
const ROOT: f32 = 40.0;
const BAND: f32 = 10.0;

#[test]
fn macros_padding_per_side_forms() {
    let mut app = Application::new(64, 64, true);

    let mut state = ();
    assert!(
        app.tick(&mut state, |_: &mut ()| {
            col! { "root", width: fixed!(ROOT), height: fixed!(ROOT), color: OUTER {
                // Array form: [top 2, right 10, bottom 4, left 6], CSS clockwise-from-top order.
                row! { "a", width: grow!(), height: fixed!(BAND), color: OUTER, padding: [2, 10, 4, 6] {
                    col! { width: grow!(), height: grow!(), color: CHILD_A {} }
                }}
                // Pair form: (vertical 3, horizontal 8), CSS's two-value shorthand.
                layout! { "b", direction: LayoutDirection::LeftToRight,
                          width: grow!(), height: fixed!(BAND), color: OUTER, padding: (3, 8) {
                    col! { width: grow!(), height: grow!(), color: CHILD_B {} }
                }}
                // Bare unsuffixed literal.
                col! { "c", width: grow!(), height: fixed!(BAND), color: OUTER, padding: 4 {
                    col! { width: grow!(), height: grow!(), color: CHILD_C {} }
                }}
                // placed! shares layout_config, so it takes the same forms. A placed child does not advance
                // flow, so it sits alone in the band.
                row! { "d", width: grow!(), height: fixed!(BAND), color: OUTER {
                    placed! { "d_placed", offset: [0.0, 0.0], width: fixed!(20.0), height: fixed!(BAND),
                              color: OUTER, padding: [1, 5, 3, 7] {
                        col! { width: grow!(), height: grow!(), color: CHILD_D {} }
                    }}
                    // A grown filler so the band's own width is unambiguous beside the placed child.
                    col! { width: grow!(), height: fit!(), color: OUTER {} }
                }}
            }}
        }),
        "first tick should keep running"
    );

    let pv = app.read_pixels().expect("render backend readback");
    assert_eq!(pv.channels, 3, "software backend reads back RGB24");

    let w = pv.width as usize;
    let px = |x: usize, y: usize| {
        let i = (y * w + x) * 3;
        [pv.pixels[i], pv.pixels[i + 1], pv.pixels[i + 2]]
    };
    let near = |p: [u8; 3], q: Color| {
        let d = |a: u8, b: u8| (a as i32 - b as i32).abs();
        d(p[0], q.r) <= 8 && d(p[1], q.g) <= 8 && d(p[2], q.b) <= 8
    };
    let check = |x: usize, y: usize, want: Color, what: &str| {
        assert!(
            near(px(x, y), want),
            "{what}: expected ({}, {}, {}) at ({x}, {y}), got {:?}",
            want.r,
            want.g,
            want.b,
            px(x, y)
        );
    };

    // Band A, y 0..10, padding [top 2, right 10, bottom 4, left 6] -> child x 6..30, y 2..6.
    check(3, 4, OUTER, "A left inset (6px)");
    check(7, 4, CHILD_A, "A content, just inside the left inset");
    check(28, 4, CHILD_A, "A content, just inside the right inset");
    check(32, 4, OUTER, "A right inset (10px)");
    check(15, 1, OUTER, "A top inset (2px)");
    check(15, 3, CHILD_A, "A content, just below the top inset");
    check(15, 5, CHILD_A, "A content, just above the bottom inset");
    check(15, 7, OUTER, "A bottom inset (4px)");

    // Band B, y 10..20, padding (vertical 3, horizontal 8) -> child x 8..32, y 13..17.
    check(5, 15, OUTER, "B left inset (8px)");
    check(9, 15, CHILD_B, "B content, just inside the left inset");
    check(30, 15, CHILD_B, "B content, just inside the right inset");
    check(34, 15, OUTER, "B right inset (8px)");
    check(20, 11, OUTER, "B top inset (3px)");
    check(20, 14, CHILD_B, "B content, just below the top inset");
    check(20, 16, CHILD_B, "B content, just above the bottom inset");
    check(20, 18, OUTER, "B bottom inset (3px)");

    // Band C, y 20..30, uniform padding 4 -> child x 4..36, y 24..26.
    check(2, 25, OUTER, "C left inset (4px)");
    check(5, 25, CHILD_C, "C content, just inside the left inset");
    check(34, 25, CHILD_C, "C content, just inside the right inset");
    check(37, 25, OUTER, "C right inset (4px)");
    check(20, 22, OUTER, "C top inset (4px)");
    check(20, 27, OUTER, "C bottom inset (4px)");

    // Band D, y 30..40. The placed child spans x 0..20 with padding [top 1, right 5, bottom 3, left 7], so its
    // own content is x 7..15, y 31..37.
    check(4, 34, OUTER, "D placed left inset (7px)");
    check(
        8,
        34,
        CHILD_D,
        "D placed content, just inside the left inset",
    );
    check(
        14,
        34,
        CHILD_D,
        "D placed content, just inside the right inset",
    );
    check(17, 34, OUTER, "D placed right inset (5px)");
    check(11, 30, OUTER, "D placed top inset (1px)");
    check(
        11,
        32,
        CHILD_D,
        "D placed content, just below the top inset",
    );
    check(
        11,
        36,
        CHILD_D,
        "D placed content, just above the bottom inset",
    );
    check(11, 38, OUTER, "D placed bottom inset (3px)");

    check(ROOT as usize + 1, 20, CLEAR, "right of the root");
}
