use flowi::{col, fixed, row, Application};

// Default clear color (theme surface.base).
const CLEAR: [u8; 3] = [14, 14, 16];
// Far from CLEAR and from each other so every region is unambiguous.
const OUTER: [u8; 3] = [220, 40, 40];
const CHILD: [u8; 3] = [40, 200, 90];

// Geometry (reference px == device px at ui_scale 1.0).
const PAD: u16 = 8;
const GAP: u16 = 6;
const CW: f32 = 12.0;
const CH: f32 = 24.0;
const OW: f32 = 2.0 * CW + GAP as f32 + 2.0 * PAD as f32; // 46
const OH: f32 = CH + 2.0 * PAD as f32; // 40

#[test]
fn macros_nested_row_col_render() {
    // ui_scale is 1.0 at this size, so reference px equal window px.
    let mut app = Application::new(64, 64, true);

    let mut state = ();
    assert!(
        app.tick(&mut state, |_: &mut ()| {
            // The single top-level element, so Clay places it at (0,0).
            row! { "root", width: fixed!(OW), height: fixed!(OH), color: [220, 40, 40], padding: 8, gap: 6 {
                // Two leaf children, ids omitted -> distinct auto file:line:column ids.
                col! { width: fixed!(CW), height: fixed!(CH), color: [40, 200, 90] {} }
                col! { width: fixed!(CW), height: fixed!(CH), color: [40, 200, 90] {} }
            }}
        }),
        "first tick should keep running"
    );

    let pv = app.read_pixels().expect("render backend readback");
    assert_eq!(pv.channels, 3, "software backend reads back RGB24");
    assert_eq!(pv.pixels.len(), 64 * 64 * 3, "tightly-packed RGB24");

    let w = pv.width as usize;
    let px = |x: usize, y: usize| {
        let i = (y * w + x) * 3;
        [pv.pixels[i], pv.pixels[i + 1], pv.pixels[i + 2]]
    };
    let near = |p: [u8; 3], q: [u8; 3]| {
        let d = |a: u8, b: u8| (a as i32 - b as i32).abs();
        d(p[0], q[0]) <= 8 && d(p[1], q[1]) <= 8 && d(p[2], q[2]) <= 8
    };

    let ax = PAD as usize; // child A left edge: 8
    let bx = (PAD as f32 + CW + GAP as f32) as usize; // child B left edge: 26
    let cy = (PAD as f32 + CH / 2.0) as usize; // vertical middle of the children: 20

    assert!(
        near(px(ax + 6, cy), CHILD),
        "child A interior should be the child fill, got {:?}",
        px(ax + 6, cy)
    );
    assert!(
        near(px(bx + 6, cy), CHILD),
        "child B interior should be the child fill, got {:?}",
        px(bx + 6, cy)
    );

    assert!(
        near(px(0, 0), OUTER),
        "top-left corner should be the outer (padding) fill, got {:?}",
        px(0, 0)
    );
    assert!(
        near(px(2, cy), OUTER),
        "left padding band should be the outer fill, got {:?}",
        px(2, cy)
    );

    let gap_mid = (PAD as f32 + CW + GAP as f32 / 2.0) as usize; // 23
    assert!(
        near(px(gap_mid, cy), OUTER),
        "gap between children should show the outer fill, got {:?}",
        px(gap_mid, cy)
    );

    assert!(
        near(px(OW as usize + 1, 0), CLEAR),
        "right of the container should be clear, got {:?}",
        px(OW as usize + 1, 0)
    );
    assert!(
        near(px(0, OH as usize + 1), CLEAR),
        "below the container should be clear, got {:?}",
        px(0, OH as usize + 1)
    );
}
