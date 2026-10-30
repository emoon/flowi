//! scroll_config builds ScrollAreaConfig and reaches the engine through Scroll::area_begin rather than
//! Layout::configure. The insets are all different and read off the scroll area's own fill, pinning the
//! `[top, right, bottom, left]` order through this path too.

use flowi::{col, fixed, grow, scroll, Application, Color};

const fn rgb(r: u8, g: u8, b: u8) -> Color {
    Color { r, g, b, a: 255 }
}

// Default clear color (theme surface.base).
const CLEAR: Color = rgb(14, 14, 16);
// The scroll area's padding bands read as this; its content child is a distinct colour.
const OUTER: Color = rgb(220, 40, 40);
const CHILD: Color = rgb(30, 180, 60);

// The scroll area is a fixed 40x40 box at (0, 0) - the single top-level element, so Clay places it there.
const ROOT: f32 = 40.0;
// A content child short enough that it never overflows, so nothing clips and every inset stays visible.
const CONTENT_H: f32 = 12.0;

#[test]
fn macros_scroll_padding_per_side() {
    let mut app = Application::new(64, 64, true);

    let mut state = ();
    assert!(
        app.tick(&mut state, |_: &mut ()| {
            // padding [top 3, right 9, bottom 5, left 6].
            scroll! { "scroller", width: fixed!(ROOT), height: fixed!(ROOT), color: OUTER,
                      padding: [3, 9, 5, 6] {
                col! { width: grow!(), height: fixed!(CONTENT_H), color: CHILD {} }
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

    // The grown-width, 12px-tall content sits inside the insets: x 6..31, y 3..15.
    check(3, 8, OUTER, "left inset (6px)");
    check(7, 8, CHILD, "content, just inside the left inset");
    check(29, 8, CHILD, "content, just inside the right inset");
    check(33, 8, OUTER, "right inset (9px)");
    check(20, 1, OUTER, "top inset (3px)");
    check(20, 4, CHILD, "content, just below the top inset");
    check(20, 14, CHILD, "content, just above its bottom edge");

    check(20, 30, OUTER, "scroll-area fill below the content");
    check(20, ROOT as usize + 1, CLEAR, "below the scroll area");
    check(ROOT as usize + 1, 8, CLEAR, "right of the scroll area");
}
