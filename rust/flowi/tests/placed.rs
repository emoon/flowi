//! placed! - a child positioned at an explicit content-space offset inside a manual_scroll clip
//! box.

use flowi::{clip, col, fixed, placed, row, Application};

// ui_scale is 1.0 at this size, so reference px equal window px.
const CLEAR: [u8; 3] = [14, 14, 16]; // default clear color (outside the clip box)
const BOX: [u8; 3] = [50, 50, 60]; // clip-box fill, where no child covers it
const FLOW: [u8; 3] = [240, 200, 40]; // the flowed sibling
const A: [u8; 3] = [40, 200, 120]; // placed at (20, 20)
const C: [u8; 3] = [80, 140, 240]; // placed at (46, 20), straddling the right clip edge

#[test]
fn placed_child_offset_and_clip() {
    let mut app = Application::new(64, 64, true);

    let mut state = ();
    assert!(
        app.tick(&mut state, move |_: &mut ()| {
            col! { "root", width: fixed!(64), height: fixed!(64), {
                // 50x50 clip box at the origin, owning its scroll.
                row! { "clipbox", width: fixed!(50), height: fixed!(50), color: [50, 50, 60],
                       clip: clip!(horizontal, vertical, manual_scroll, scroll_pos: [0.0, 0.0]), {
                    // Sits at the flex-cursor origin (0,0); placed children must not displace it.
                    row! { width: fixed!(8), height: fixed!(8), color: [240, 200, 40] {} }
                    // Explicit content-space offset, not flex flow.
                    placed! { offset: [20.0, 20.0], width: fixed!(8), height: fixed!(8), color: [40, 200, 120] {} }
                    // Placed at (46,20): straddles the right clip edge (46..54 vs box 0..50) - its tail is cropped.
                    placed! { offset: [46.0, 20.0], width: fixed!(8), height: fixed!(8), color: [80, 140, 240] {} }
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
    let near = |p: [u8; 3], q: [u8; 3]| {
        let d = |a: u8, b: u8| (a as i32 - b as i32).abs();
        d(p[0], q[0]) <= 8 && d(p[1], q[1]) <= 8 && d(p[2], q[2]) <= 8
    };

    // A's box is 20..28, so (24,24) is its interior.
    assert!(
        near(px(24, 24), A),
        "placed child renders at its offset, got {:?}",
        px(24, 24)
    );
    assert!(
        near(px(4, 4), FLOW),
        "flowed sibling stays at origin, got {:?}",
        px(4, 4)
    );
    assert!(
        near(px(35, 40), BOX),
        "clip box renders its own area, got {:?}",
        px(35, 40)
    );
    assert!(
        near(px(48, 24), C),
        "placed child draws inside the clip area, got {:?}",
        px(48, 24)
    );
    assert!(
        !near(px(52, 24), C),
        "placed child is clipped to the container, got {:?}",
        px(52, 24)
    );
    assert!(
        near(px(52, 24), CLEAR),
        "outside the clip box is the clear color, got {:?}",
        px(52, 24)
    );
}
