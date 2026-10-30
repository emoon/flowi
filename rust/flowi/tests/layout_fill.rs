//! fill(), the flexible spacer. It contributes no minimum, so a fit!-height row around it stays
//! the height of its 10px child; it pushes siblings apart, landing the child after it flush
//! against the container's trailing edge; and it carries no id, taking a parent-scoped
//! structural id from the child index instead.
//!
//! The spacer is transparent, so it is observed only through the geometry of its siblings.
//! A duplicate id aborts the process in a debug build, so reaching the assertions shows none
//! collided.

use flowi::{col, fill, fit, fixed, row, Application, Color};

const fn rgb(r: u8, g: u8, b: u8) -> Color {
    Color { r, g, b, a: 255 }
}

// Default clear color (theme surface.base).
const CLEAR: Color = rgb(14, 14, 16);
const OUTER: Color = rgb(220, 40, 40);
const LEADING: Color = rgb(30, 180, 60);
const TRAILING: Color = rgb(40, 80, 220);

const ROOT_W: f32 = 40.0;
const BOX: f32 = 10.0;

/// Each call is a distinct child of the enclosing container, so each gets its own structural id.
fn gap() {
    fill();
}

#[test]
fn fill_spaces_without_growing_a_fit_parent() {
    let mut app = Application::new(64, 64, true);

    let mut state = ();
    assert!(
        app.tick(&mut state, |_: &mut ()| {
            // The spacer between the two boxes eats all 20px of slack; if its cross-axis grow contributed a
            // minimum, the row would stretch.
            row! { "root", width: fixed!(ROOT_W), height: fit!(), color: OUTER {
                col! { "leading", width: fixed!(BOX), height: fixed!(BOX), color: LEADING {} }
                fill();
                col! { "trailing", width: fixed!(BOX), height: fixed!(BOX), color: TRAILING {} }

                // A zero-height sibling, so the loop and helper spacers cannot disturb the geometry above.
                col! { "id_probe", width: fixed!(1.0), height: fixed!(0.0) {
                    for _ in 0..3 {
                        fill();
                    }
                    gap();
                    gap();
                    gap();
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

    // The trailing box is pushed flush right (x 30..40).
    check(5, 5, LEADING, "leading box");
    check(35, 5, TRAILING, "trailing box, pushed to the right edge");
    check(20, 5, OUTER, "spacer is transparent — the row's fill shows");
    check(28, 5, OUTER, "row fill, just left of the trailing box");

    check(20, 9, OUTER, "last row of the fitted 10px height");
    check(
        20,
        11,
        CLEAR,
        "below a fit! row: the spacer did not grow it",
    );
    check(
        5,
        11,
        CLEAR,
        "below the leading box: the row stopped at 10px",
    );
}
