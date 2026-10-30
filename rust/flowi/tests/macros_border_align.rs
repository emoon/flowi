use flowi::{align, border, col, fixed, Application};

const BG: [u8; 3] = [40, 40, 60];
const BORDER: [u8; 3] = [255, 255, 80];
const CHILD: [u8; 3] = [30, 180, 60];

const CONT: f32 = 40.0; // container, top-left at (0,0)
const BW: u16 = 4;

#[test]
fn macros_border_align_render() {
    let mut app = Application::new(64, 64, true);

    let mut state = ();
    assert!(
        app.tick(&mut state, |_: &mut ()| {
            col! { "boxed", width: fixed!(CONT), height: fixed!(CONT), color: [40, 40, 60],
                   border: border!([255, 255, 80], BW), align: align!(Right, Bottom) {
                col! { width: fixed!(10.0), height: fixed!(10.0), color: [30, 180, 60] {} }
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

    // Sampled inside the 4px border band, off the corners.
    assert!(
        near(px(1, 20), BORDER),
        "left border edge should be the border color, got {:?}",
        px(1, 20)
    );
    assert!(
        near(px(20, 1), BORDER),
        "top border edge should be the border color, got {:?}",
        px(20, 1)
    );
    assert!(
        near(px(CONT as usize - 2, 20), BORDER),
        "right border edge should be the border color, got {:?}",
        px(CONT as usize - 2, 20)
    );
    assert!(
        near(px(20, CONT as usize - 2), BORDER),
        "bottom border edge should be the border color, got {:?}",
        px(20, CONT as usize - 2)
    );

    assert!(
        near(px(33, 33), CHILD),
        "child should be aligned to the bottom-right, got {:?}",
        px(33, 33)
    );
    assert!(
        near(px(8, 8), BG),
        "top-left interior should be bare bg (child aligned away), got {:?}",
        px(8, 8)
    );
}
