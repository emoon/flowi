use flowi::{fit, fixed, layout, percent, Application, LayoutDirection};

const CLEAR: [u8; 3] = [14, 14, 16];
const OUTER: [u8; 3] = [200, 30, 30];
const CHILD_A: [u8; 3] = [30, 180, 60];
const CHILD_B: [u8; 3] = [40, 80, 220];

// Root is a fixed 40x40 box at (0,0); children stack top-to-bottom, each 10px tall.
const ROOT: f32 = 40.0;

#[test]
fn macros_layout_percent_fit_sizing() {
    let mut app = Application::new(64, 64, true);

    let mut state = ();
    assert!(
        app.tick(&mut state, |_: &mut ()| {
            layout! { "root", direction: LayoutDirection::TopToBottom,
                      width: fixed!(ROOT), height: fixed!(ROOT), color: [200, 30, 30] {
                // percent!(0.5): half the 40px parent width -> ~20px, at the top row.
                layout! { width: percent!(0.5), height: fixed!(10.0), color: [30, 180, 60] {} }
                // fit!(6.0, 30.0): empty content fits to 0, clamped up to the 6px min, in the second row.
                layout! { width: fit!(6.0, 30.0), height: fixed!(10.0), color: [40, 80, 220] {} }
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
    let near = |p: [u8; 3], q: [u8; 3]| {
        let d = |a: u8, b: u8| (a as i32 - b as i32).abs();
        d(p[0], q[0]) <= 8 && d(p[1], q[1]) <= 8 && d(p[2], q[2]) <= 8
    };

    // ~20px wide in the top row: inside at x=10, parent fill by x=30.
    assert!(
        near(px(10, 5), CHILD_A),
        "percent! child interior should be its fill, got {:?}",
        px(10, 5)
    );
    assert!(
        near(px(30, 5), OUTER),
        "beyond the 50% percent! width should be the parent fill, got {:?}",
        px(30, 5)
    );

    // Clamped to its 6px min in the second row: inside at x=3, parent fill well past it.
    assert!(
        near(px(3, 15), CHILD_B),
        "fit! child interior should be its fill, got {:?}",
        px(3, 15)
    );
    assert!(
        near(px(20, 15), OUTER),
        "beyond the fit! min width should be the parent fill, got {:?}",
        px(20, 15)
    );

    assert!(
        near(px(ROOT as usize + 1, 0), CLEAR),
        "right of the root should be clear, got {:?}",
        px(ROOT as usize + 1, 0)
    );
}
