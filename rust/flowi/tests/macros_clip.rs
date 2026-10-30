use flowi::{clip, col, fixed, Application};

const CLEAR: [u8; 3] = [14, 14, 16];
const BG: [u8; 3] = [60, 60, 60];
const CHILD: [u8; 3] = [30, 180, 60];

const CONT: f32 = 40.0; // clipping container, top-left at (0,0)
const CW: f32 = 20.0;

#[test]
fn macros_clip_manual_scroll_render() {
    let mut app = Application::new(64, 64, true);

    let px_at = |pv: &flowi::PixelView, x: usize, y: usize| {
        let w = pv.width as usize;
        let i = (y * w + x) * 3;
        [pv.pixels[i], pv.pixels[i + 1], pv.pixels[i + 2]]
    };
    let near = |p: [u8; 3], q: [u8; 3]| {
        let d = |a: u8, b: u8| (a as i32 - b as i32).abs();
        d(p[0], q[0]) <= 8 && d(p[1], q[1]) <= 8 && d(p[2], q[2]) <= 8
    };

    // Tick 1: scroll_pos.x = 0.
    let mut state = ();
    assert!(
        app.tick(&mut state, |_: &mut ()| {
            col! { "lane", width: fixed!(CONT), height: fixed!(CONT), color: [60, 60, 60],
                   clip: clip!(horizontal, manual_scroll, scroll_pos: [0.0f32, 0.0f32]) {
                col! { width: fixed!(CW), height: fixed!(CONT), color: [30, 180, 60] {} }
            }}
        }),
        "first tick should keep running"
    );
    let pv = app.read_pixels().expect("render backend readback");
    assert_eq!(pv.channels, 3, "software backend reads back RGB24");
    assert!(
        near(px_at(&pv, 5, 20), CHILD),
        "child should be flush left at scroll 0, got {:?}",
        px_at(&pv, 5, 20)
    );
    assert!(
        near(px_at(&pv, 30, 20), BG),
        "right of the child (inside the container) is the container bg, got {:?}",
        px_at(&pv, 30, 20)
    );
    assert!(
        near(px_at(&pv, CONT as usize + 2, 20), CLEAR),
        "past the container is clear, got {:?}",
        px_at(&pv, CONT as usize + 2, 20)
    );

    // Tick 2: scroll_pos.x = 25 shifts the child right by 25px, so its right edge (now at 45) is cut at the
    // 40px container edge.
    assert!(
        app.tick(&mut state, |_: &mut ()| {
            col! { "lane", width: fixed!(CONT), height: fixed!(CONT), color: [60, 60, 60],
                   clip: clip!(horizontal, manual_scroll, scroll_pos: [25.0f32, 0.0f32]) {
                col! { width: fixed!(CW), height: fixed!(CONT), color: [30, 180, 60] {} }
            }}
        }),
        "second tick should keep running"
    );
    let pv = app.read_pixels().expect("render backend readback");
    assert!(
        near(px_at(&pv, 5, 20), BG),
        "left should be bare bg after scrolling the child right, got {:?}",
        px_at(&pv, 5, 20)
    );
    assert!(
        near(px_at(&pv, 30, 20), CHILD),
        "child should have moved into the right band, got {:?}",
        px_at(&pv, 30, 20)
    );
    assert!(
        near(px_at(&pv, CONT as usize + 2, 20), CLEAR),
        "clipped overflow must not spill past the container, got {:?}",
        px_at(&pv, CONT as usize + 2, 20)
    );
}
