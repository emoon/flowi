//! Debug builds abort on duplicate element ids, so a clean tick that renders each container in its own place
//! shows the seeds produced distinct hashes.

use flowi::{col, fixed, Application};

const CLEAR: [u8; 3] = [14, 14, 16];
const CHILD: [u8; 3] = [40, 200, 90];

const N: usize = 3;
const CW: f32 = 40.0;
const CH: f32 = 12.0;
const GAP: u16 = 4;
const OW: f32 = CW;
const OH: f32 = N as f32 * CH + (N as f32 - 1.0) * GAP as f32; // 44

#[test]
fn seeded_tuple_ids_disambiguate_repeated_names() {
    // ui_scale is 1.0 at this size, so reference px equal window px.
    let mut app = Application::new(64, 64, true);

    let mut state = ();
    assert!(
        app.tick(&mut state, |_: &mut ()| {
            col! { "root", width: fixed!(OW), height: fixed!(OH), gap: GAP {
                // Same name every lane; the tuple's integer seed is what keeps the element ids distinct. Each
                // lane uses a different seed integer type to cover every accepted `From<(&str, _)>` impl.
                col! { ("lane", 0usize), width: fixed!(CW), height: fixed!(CH), color: [40, 200, 90] {} }
                col! { ("lane", 1i32),   width: fixed!(CW), height: fixed!(CH), color: [40, 200, 90] {} }
                col! { ("lane", 2u32),   width: fixed!(CW), height: fixed!(CH), color: [40, 200, 90] {} }
            }}
        }),
        "tick with three same-name seeded containers should keep running (no duplicate-id abort)"
    );

    let pv = app.read_pixels().expect("render backend readback");
    let w = pv.width as usize;
    let px = |x: usize, y: usize| {
        let i = (y * w + x) * 3;
        [pv.pixels[i], pv.pixels[i + 1], pv.pixels[i + 2]]
    };
    let near = |p: [u8; 3], q: [u8; 3]| {
        let d = |a: u8, b: u8| (a as i32 - b as i32).abs();
        d(p[0], q[0]) <= 8 && d(p[1], q[1]) <= 8 && d(p[2], q[2]) <= 8
    };

    let cx = (CW / 2.0) as usize;
    for i in 0..N {
        let cy = (i as f32 * (CH + GAP as f32) + CH / 2.0) as usize;
        assert!(
            near(px(cx, cy), CHILD),
            "lane {i} interior should be the child fill, got {:?}",
            px(cx, cy)
        );
    }

    let gap_y = (CH + GAP as f32 / 2.0) as usize;
    assert!(
        near(px(cx, gap_y), CLEAR),
        "gap between lanes should be clear, got {:?}",
        px(cx, gap_y)
    );
}
